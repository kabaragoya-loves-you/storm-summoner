#include "scene.h"
#include "touchwheel.h"
#include "touch.h"
#include "esp_log.h"
#include "midi_messages.h"
#include "device_config.h"
#include "config.h"
#include "app_settings.h"
#include "event_bus.h"
#include "action.h"
#include "tempo.h"
#include "input_manager.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "scene";

// Scene storage paths
#define SCENES_BASE_PATH "/assets/scenes"
#define MANIFEST_PATH "/assets/scenes/manifest.json"

// Forward declarations
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size);
static esp_err_t json_to_scene(cJSON* root, scene_t* scene);
static void scene_init_defaults(scene_t* scene, uint8_t index);
static void scene_cleanup_touchwheel(void);
static void scene_setup_touchwheel_for_mode(const scene_t* scene);

// NVS keys
#define NVS_KEY_SCENE_MODE       "scene_mode"
#define NVS_KEY_CHANGE_MODE      "change_mode"
#define NVS_KEY_AUTOSAVE_MODE    "autosave_mode"

// Global scene manager instance
static scene_manager_t g_scene_manager = {
  .current_cache_idx = 0,
  .current_scene_index = 0,
  .pending_scene_index = 0,
  .has_pending_change = false,
  .manifest = NULL,
  .num_scenes = 1,
  .mode = SCENE_MODE_SINGLE,
  .change_mode = CHANGE_MODE_IMMEDIATE,
  .autosave_mode = SCENE_AUTOSAVE_MANUAL,
  .initialized = false
};

// Touchwheel instance for scene encoder mode
static touchwheel_instance_t* s_scene_touchwheel = NULL;

// Default CC assignments for initial testing
static const uint8_t DEFAULT_CC_NUMBERS[NUM_TOUCHPADS] = {
  1, 2, 3, 4, 5, 6, 7, 8,     // Touchwheel pads (0-7)
  9, 10, 11, 12               // Additional pads (8-11)
};

// Helper: Get scene by index (returns current scene if it matches, otherwise error)
// For now, we only allow modifications to the current scene
static scene_t* get_scene_for_modification(uint8_t scene_index) {
  if (scene_index != g_scene_manager.current_scene_index) {
    ESP_LOGW(TAG, "Can only modify current scene (currently on %d, requested %d)", 
             g_scene_manager.current_scene_index, scene_index);
    return NULL;
  }
  
  return scene_get_current();
}

// Set default button assignments based on scene mode
static void set_default_button_assignments(scene_t* scene) {
  scene_mode_t mode = g_scene_manager.mode;
  
  if (mode == SCENE_MODE_SINGLE) {
    // Mode 1: Buttons control program changes
    scene->button_left.num_actions = 1;
    scene->button_left.actions[0] = action_create_program_prev();
    
    scene->button_right.num_actions = 1;
    scene->button_right.actions[0] = action_create_program_next();
    
    scene->button_both.num_actions = 1;
    scene->button_both.actions[0].type = ACTION_CONFIRM_PENDING;
  } else {
    // Modes 2 & 3: Buttons control scene navigation
    scene->button_left.num_actions = 1;
    scene->button_left.actions[0] = action_create_scene_prev();
    
    scene->button_right.num_actions = 1;
    scene->button_right.actions[0] = action_create_scene_next();
    
    scene->button_both.num_actions = 1;
    scene->button_both.actions[0].type = ACTION_CONFIRM_PENDING;
  }
}

// Initialize a single scene with defaults
static void scene_init_defaults(scene_t* scene, uint8_t index) {
  memset(scene, 0, sizeof(scene_t));
  
  // Set default name
  snprintf(scene->name, sizeof(scene->name), "Scene %d", index + 1);
  
  // Program change defaults
  scene->program_number = index;  // Match scene index in preset sync mode
  scene->send_pc_on_load = true;
  
  // Default touchwheel mode
  scene->touchwheel_mode = TOUCHWHEEL_MODE_BUTTONS;
  scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;  // Default: position-based (~15 values)
  scene->touchwheel = continuous_mapping_create(16);    // CC16 = General Purpose 1
  scene->touchwheel.enabled = false;                    // Disabled by default (BUTTONS mode)
  
  // Initialize touchpad mappings with default CC actions
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    scene->touchpads[i].actions.num_actions = 1;
    scene->touchpads[i].actions.actions[0] = action_create_send_cc(DEFAULT_CC_NUMBERS[i], 127);
  }
  
  // Set default button assignments
  set_default_button_assignments(scene);
  
  // Initialize on_load actions (default: All Notes Off for clean slate)
  scene->on_load.num_actions = 1;
  scene->on_load.actions[0] = action_create_all_notes_off();
  
  // Initialize discrete trigger inputs
  scene->bump.num_actions = 1;
  scene->bump.actions[0] = action_create_tap_tempo();  // Default: tap tempo on bump
  
  // Initialize continuous input mappings with defaults
  scene->expression = continuous_mapping_create(4);    // CC4 = Foot Controller (expression pedal)
  scene->cv = continuous_mapping_create(16);           // CC16 = General Purpose 1
  scene->proximity = continuous_mapping_create(17);    // CC17 = General Purpose 2
  scene->proximity.use_idle_value = true;              // Proximity returns to center
  scene->proximity.idle_value = 64;                    // Center for CC (60 for NOTE mode)
  scene->proximity.idle_timeout_ms = 1000;
  scene->proximity.polarity = POLARITY_BIPOLAR;
  scene->als = continuous_mapping_create(18);          // CC18 = General Purpose 3
  
  // Expression jack configuration
  scene->expression_mode = EXPRESSION_MODE_PEDAL;      // Default to expression pedal mode
  
  // Default sustain action: ACTION_SUSTAIN (CC64 toggle)
  scene->sustain.num_actions = 1;
  scene->sustain.actions[0] = action_create_sustain();
  
  // Default sostenuto action: ACTION_SOSTENUTO (CC66 toggle)
  scene->sostenuto.num_actions = 1;
  scene->sostenuto.actions[0] = action_create_sostenuto();
  
  // CV input configuration
  scene->cv_input_mode = INPUT_MODE_CV;                // Default to CV mode
  
  // NOTE mode configuration
  scene->note_velocity_mode = VELOCITY_MODE_FIXED;     // Default to fixed velocity
  scene->note_fixed_velocity = 100;                    // Default velocity value
  
  // Tempo configuration
  scene->clock_source = CLOCK_SOURCE_INTERNAL;         // Default to internal clock
  scene->clock_standard = CLOCK_STANDARD_24PPQN;       // Default to MIDI standard
  scene->time_signature.numerator = 4;                 // Default to 4/4 time
  scene->time_signature.denominator = 4;
}

// Cleanup existing touchwheel instance
static void scene_cleanup_touchwheel(void) {
  if (s_scene_touchwheel) {
    touch_unregister_touchwheel_instance(s_scene_touchwheel);
    touchwheel_destroy(s_scene_touchwheel);
    s_scene_touchwheel = NULL;
  }
}

// Callback for program change mode touchwheel
static void touchwheel_program_change_callback(int value, void* user_data) {
  (void)user_data;
  
  // value is delta from endless encoder (+1, -1, etc.)
  if (value == 0) return;
  
  // Check if bank mode is enabled for extended preset range
  bank_select_mode_t bank_mode = device_config_get_bank_mode();
  
  if (bank_mode != BANK_SELECT_NONE) {
    // Bank mode: use preset-based calculation (0-16383 range)
    uint16_t base_preset = device_config_has_pending_program()
                           ? device_config_get_pending_preset()
                           : device_config_get_preset();
    int new_preset = (int)base_preset + value;
    
    // Clamp at boundaries (no wrap in bank mode)
    if (new_preset < 0) new_preset = 0;
    if (new_preset > 16383) new_preset = 16383;
    
    if ((uint16_t)new_preset == base_preset) return;
    
    // Respect immediate/pending mode
    if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
      device_config_set_preset((uint16_t)new_preset);
      ESP_LOGD(TAG, "Touchwheel preset change: %u -> %d (bank %d, prog %d)", 
               (unsigned)base_preset, new_preset, new_preset / 128, new_preset % 128);
    } else {
      device_config_set_pending_preset((uint16_t)new_preset);
      ESP_LOGI(TAG, "Touchwheel pending preset: %d (bank %d, prog %d)", 
               new_preset, new_preset / 128, new_preset % 128);
    }
    return;
  }
  
  // No bank mode: original 0-127 behavior
  uint8_t base = device_config_has_pending_program() 
                 ? device_config_get_pending_program() 
                 : device_config_get_program();
  int new_program = (int)base + value;
  
  // Apply wrap or clamp based on setting
  if (config_get_program_wrap()) {
    // Wrap around
    while (new_program < 0) new_program += 128;
    while (new_program > 127) new_program -= 128;
  } else {
    // Clamp at boundaries
    if (new_program < 0) new_program = 0;
    if (new_program > 127) new_program = 127;
  }
  
  if ((uint8_t)new_program == base) return;
  
  // Respect immediate/pending mode
  if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
    device_config_set_program((uint8_t)new_program);
    ESP_LOGD(TAG, "Touchwheel program change: %d -> %d", base, new_program);
  } else {
    // Set as pending - will be confirmed by confirm_pending action
    device_config_set_pending_program((uint8_t)new_program);
    ESP_LOGI(TAG, "Touchwheel pending program: %d (confirm to send)", new_program);
  }
}

// Tracked value for endless encoder continuous mode
static int s_touchwheel_endless_value = 64;  // Start at center

// Callback for continuous mode touchwheel (CC/Note output)
static void touchwheel_continuous_callback(int value, void* user_data) {
  scene_t* scene = (scene_t*)user_data;
  if (!scene || !scene->touchwheel.enabled) return;
  
  uint8_t midi_value;
  
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is a delta (+1, -1, etc.)
    s_touchwheel_endless_value += value;
    // Clamp to 0-127
    if (s_touchwheel_endless_value < 0) s_touchwheel_endless_value = 0;
    if (s_touchwheel_endless_value > 127) s_touchwheel_endless_value = 127;
    midi_value = (uint8_t)s_touchwheel_endless_value;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    midi_value = (uint8_t)((value * 127) / 100);
  }
  
  // Process through continuous mapping (applies curve, polarity, scaling)
  uint8_t output = continuous_mapping_process(midi_value, &scene->touchwheel);
  
  // Send MIDI based on output type
  uint8_t channel = device_config_get_channel() - 1;
  
  if (scene->touchwheel.output_type == OUTPUT_TYPE_CC) {
    continuous_mapping_send_cc(&scene->touchwheel, channel, output);
    ESP_LOGD(TAG, "Touchwheel CC = %d", output);
  } else {
    // Note mode: convert value to note number
    uint8_t note = continuous_mapping_value_to_note(output, &scene->touchwheel);
    // Note handling is done via the touch events (on/off), this just updates the target note
    ESP_LOGD(TAG, "Touchwheel note target: %d", note);
  }
}

// Setup touchwheel instance based on scene mode
static void scene_setup_touchwheel_for_mode(const scene_t* scene) {
  if (!scene) return;
  
  touchwheel_mode_processor_t* mode_proc = NULL;
  touchwheel_output_t* output = NULL;
  const char* mode_desc = NULL;
  
  switch (scene->touchwheel_mode) {
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE:
      mode_proc = touchwheel_mode_create_endless();
      output = touchwheel_output_callback_create(touchwheel_program_change_callback, NULL);
      mode_desc = "program_change";
      break;
      
    case TOUCHWHEEL_MODE_CONTINUOUS:
      // Choose between odometer and endless based on style setting
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "continuous (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "continuous (odometer)";
      }
      output = touchwheel_output_callback_create(touchwheel_continuous_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_BUTTONS:
    default:
      // No touchwheel instance needed for buttons mode
      return;
  }
  
  if (mode_proc && output) {
    s_scene_touchwheel = touchwheel_create(mode_proc, output, 500);  // 500ms timeout
    if (s_scene_touchwheel) {
      touch_register_touchwheel_instance(s_scene_touchwheel);
      ESP_LOGI(TAG, "Created touchwheel instance for %s mode", mode_desc);
    } else {
      touchwheel_mode_destroy(mode_proc);
      touchwheel_output_destroy(output);
      ESP_LOGE(TAG, "Failed to create touchwheel instance");
    }
  } else {
    if (mode_proc) touchwheel_mode_destroy(mode_proc);
    if (output) touchwheel_output_destroy(output);
    ESP_LOGE(TAG, "Failed to create touchwheel mode or output");
  }
}

esp_err_t scene_init(void) {
  if (g_scene_manager.initialized) {
    ESP_LOGW(TAG, "Scene manager already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing scene manager with flash-based storage");
  
  // Load scene mode from NVS
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  // Load change mode from NVS
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  // Load autosave mode from NVS
  uint8_t autosave_val;
  if (app_settings_load_u8(NVS_KEY_AUTOSAVE_MODE, &autosave_val) == ESP_OK) {
    g_scene_manager.autosave_mode = (scene_autosave_mode_t)autosave_val;
  }
  
  // Initialize cache
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    g_scene_manager.cache[i].valid = false;
    g_scene_manager.cache[i].dirty = false;
    g_scene_manager.cache[i].index = 0;
  }
  
  // Load or create scene manifest
  esp_err_t ret = scene_load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load manifest, creating default");
    // Create default manifest with one scene
    g_scene_manager.manifest = malloc(sizeof(scene_manifest_entry_t));
    if (!g_scene_manager.manifest) {
      ESP_LOGE(TAG, "Failed to allocate manifest");
      return ESP_ERR_NO_MEM;
    }
    g_scene_manager.num_scenes = 1;
    g_scene_manager.manifest[0].index = 0;
    strncpy(g_scene_manager.manifest[0].name, "Scene 1", sizeof(g_scene_manager.manifest[0].name));
    strncpy(g_scene_manager.manifest[0].filename, "scene_001.json", sizeof(g_scene_manager.manifest[0].filename));
  }
  
  // Load first scene into cache slot 0
  g_scene_manager.current_cache_idx = 0;
  g_scene_manager.current_scene_index = 0;
  
  // Load scene 0 directly into cache[0] (NOT using scene_load_from_flash which uses wrong slot)
  char filepath[128];
  get_scene_filename(0, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json_str = malloc(fsize + 1);
    if (json_str) {
      fread(json_str, 1, fsize, f);
      fclose(f);
      json_str[fsize] = '\0';
      
      cJSON* root = cJSON_Parse(json_str);
      free(json_str);
      
      if (root) {
        ret = json_to_scene(root, &g_scene_manager.cache[0].scene);
        cJSON_Delete(root);
        
        if (ret == ESP_OK) {
          ESP_LOGI(TAG, "Loaded scene 0 from flash");
        } else {
          ESP_LOGW(TAG, "Failed to parse scene 0, using defaults");
          scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
        }
      } else {
        ESP_LOGW(TAG, "Failed to parse scene 0 JSON, using defaults");
        scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
      }
    } else {
      fclose(f);
      ESP_LOGW(TAG, "Failed to allocate memory for scene 0, using defaults");
      scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
    }
  } else {
    ESP_LOGW(TAG, "Scene 0 file not found, using defaults");
    scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
  }
  
  g_scene_manager.cache[0].index = 0;
  g_scene_manager.cache[0].valid = true;
  g_scene_manager.cache[0].dirty = false;
  
  g_scene_manager.initialized = true;
  
  const char* mode_str = (g_scene_manager.mode == SCENE_MODE_SINGLE) ? "Single" :
                         (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene manager initialized: mode=%s, total_scenes=%d", mode_str, g_scene_manager.num_scenes);
  
  // Initialize device current_program from scene's program_number
  scene_t* initial_scene = &g_scene_manager.cache[0].scene;
  uint8_t initial_program = (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? 0 : initial_scene->program_number;
  device_config_set_program(initial_program);
  
  // Log PC send status
  if (initial_scene->send_pc_on_load) {
    ESP_LOGI(TAG, "Sent initial PC %d on channel %d", initial_program, device_config_get_channel());
  } else {
    ESP_LOGI(TAG, "Scene loaded but send_pc_on_load=false, PC not sent");
    // Note: device_config_set_program already sent it, so we'd need to track this better
    // For now, PC is always sent on boot
  }
  
  // Configure tempo settings for initial scene
  tempo_set_source(initial_scene->clock_source);
  tempo_set_clock_standard(initial_scene->clock_standard);
  tempo_set_time_signature(initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set initial tempo: source=%d, standard=%d, time_sig=%d/%d", 
           initial_scene->clock_source, initial_scene->clock_standard,
           initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  
  // Execute on_load actions
  if (initial_scene->on_load.num_actions > 0) {
    ESP_LOGI(TAG, "Executing %d on_load action(s)", initial_scene->on_load.num_actions);
    action_execute_chain(&initial_scene->on_load, 127, true);
  }
  
  // Setup touchwheel instance for non-buttons modes
  scene_setup_touchwheel_for_mode(initial_scene);
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = 0}
  };
  event_bus_post(&event);
  
  return ESP_OK;
}

esp_err_t scene_set_current(uint8_t scene_index) {
  if (!g_scene_manager.initialized) {
    ESP_LOGE(TAG, "Scene manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (scene_index > MAX_SCENE_INDEX) {
    ESP_LOGE(TAG, "Invalid scene index %d", scene_index);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Check if scene exists in manifest
  bool scene_exists = false;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      scene_exists = true;
      break;
    }
  }
  
  if (!scene_exists) {
    ESP_LOGE(TAG, "Scene %d does not exist in manifest", scene_index);
    return ESP_ERR_NOT_FOUND;
  }
  
  // In pending mode, set pending instead of changing immediately
  if (g_scene_manager.change_mode == CHANGE_MODE_PENDING) {
    g_scene_manager.pending_scene_index = scene_index;
    g_scene_manager.has_pending_change = true;
    ESP_LOGI(TAG, "Pending scene change to %d (confirm or cancel)", scene_index + 1);
    return ESP_OK;
  }
  
  if (g_scene_manager.current_scene_index == scene_index) {
    return ESP_OK;  // Already on this scene
  }
  
  // Check if scene is already in cache
  int cache_idx = -1;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      cache_idx = i;
      break;
    }
  }
  
  // If not in cache, need to load it
  if (cache_idx == -1) {
    // Find least recently used cache slot (for now, just use round-robin)
    cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
    
    // Save current cache entry if dirty (respects autosave mode)
    if (g_scene_manager.cache[cache_idx].valid && g_scene_manager.cache[cache_idx].dirty) {
      if (g_scene_manager.autosave_mode == SCENE_AUTOSAVE_AUTO) {
        scene_save_to_flash(g_scene_manager.cache[cache_idx].index);
        ESP_LOGI(TAG, "Auto-saved scene %d", g_scene_manager.cache[cache_idx].index + 1);
      } else {
        ESP_LOGW(TAG, "Scene %d has unsaved changes (autosave=manual, use 'save' command)", g_scene_manager.cache[cache_idx].index + 1);
      }
    }
    
    // Load new scene into cache
    esp_err_t ret = scene_load_from_flash(scene_index);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to load scene %d, using defaults", scene_index);
      scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
    }
    
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
    g_scene_manager.cache[cache_idx].dirty = false;
  }
  
  g_scene_manager.current_cache_idx = cache_idx;
  g_scene_manager.current_scene_index = scene_index;
  
  scene_t* new_scene = &g_scene_manager.cache[cache_idx].scene;
  
  // Update device current_program and send PC based on mode
  uint8_t program = (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? scene_index : new_scene->program_number;
  
  if (new_scene->send_pc_on_load) {
    device_config_set_program(program);
    ESP_LOGD(TAG, "Sent PC %d on channel %d", program, device_config_get_channel());
  } else {
    // Scene doesn't send PC on load - skip
    ESP_LOGD(TAG, "Scene send_pc_on_load=false, no PC sent");
  }
  
  ESP_LOGI(TAG, "Switched to scene %d: %s", scene_index + 1, new_scene->name);
  
  // Configure expression jack mode for this scene
  expression_set_mode(new_scene->expression_mode);
  
  // Configure tempo settings for this scene
  tempo_set_source(new_scene->clock_source);
  tempo_set_clock_standard(new_scene->clock_standard);
  tempo_set_time_signature(new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set tempo: source=%d, standard=%d, time_sig=%d/%d", 
           new_scene->clock_source, new_scene->clock_standard,
           new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  
  // Execute on_load actions
  if (new_scene->on_load.num_actions > 0) {
    ESP_LOGD(TAG, "Executing %d on_load action(s)", new_scene->on_load.num_actions);
    action_execute_chain(&new_scene->on_load, 127, true);
  }
  
  // Setup touchwheel instance for non-buttons modes
  scene_cleanup_touchwheel();
  scene_setup_touchwheel_for_mode(new_scene);
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);
  
  return ESP_OK;
}

uint8_t scene_get_current_index(void) {
  return g_scene_manager.current_scene_index;
}

scene_t* scene_get_current(void) {
  if (!g_scene_manager.initialized) return NULL;
  
  // Return current scene from cache
  int idx = g_scene_manager.current_cache_idx;
  if (idx >= 0 && idx < SCENE_CACHE_SIZE && g_scene_manager.cache[idx].valid) {
    return &g_scene_manager.cache[idx].scene;
  }
  
  return NULL;
}

esp_err_t scene_next(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to next in manifest (wrap around)
  int next_pos = (current_pos + 1) % g_scene_manager.num_scenes;
  return scene_set_current(g_scene_manager.manifest[next_pos].index);
}

esp_err_t scene_previous(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to previous in manifest (wrap around)
  int prev_pos = (current_pos == 0) ? g_scene_manager.num_scenes - 1 : current_pos - 1;
  return scene_set_current(g_scene_manager.manifest[prev_pos].index);
}

esp_err_t scene_set_name(uint8_t scene_index, const char* name) {
  if (scene_index > MAX_SCENE_INDEX || !name) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  strncpy(scene->name, name, sizeof(scene->name) - 1);
  scene->name[sizeof(scene->name) - 1] = '\0';
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Scene %d renamed to: %s", scene_index + 1, name);
  return ESP_OK;
}

esp_err_t scene_set_mode(scene_mode_t mode) {
  g_scene_manager.mode = mode;
  
  const char* mode_str = (mode == SCENE_MODE_SINGLE) ? "Single" :
                         (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene mode set to %s", mode_str);
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
}

scene_mode_t scene_get_mode(void) {
  return g_scene_manager.mode;
}

esp_err_t scene_set_change_mode(scene_change_mode_t mode) {
  g_scene_manager.change_mode = mode;
  
  ESP_LOGI(TAG, "Change mode set to %s", mode == CHANGE_MODE_IMMEDIATE ? "immediate" : "pending");
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, mode_val);
}

scene_change_mode_t scene_get_change_mode(void) {
  return g_scene_manager.change_mode;
}

esp_err_t scene_set_autosave_mode(scene_autosave_mode_t mode) {
  g_scene_manager.autosave_mode = mode;
  
  ESP_LOGI(TAG, "Autosave mode set to %s", mode == SCENE_AUTOSAVE_MANUAL ? "manual" : "auto");
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_AUTOSAVE_MODE, mode_val);
}

scene_autosave_mode_t scene_get_autosave_mode(void) {
  return g_scene_manager.autosave_mode;
}

esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program) {
  if (scene_index > MAX_SCENE_INDEX || program > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->program_number = program;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // Also update device's current_program and send PC
  device_config_set_program(program);
  
  ESP_LOGI(TAG, "Scene %d program number set to %d (PC sent)", scene_index + 1, program);
  return ESP_OK;
}

esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  // Get current scene if it matches
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->send_pc_on_load = send_pc;
    g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  } else {
    ESP_LOGW(TAG, "Can only modify current scene (load scene %d first)", scene_index);
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Scene %d send PC on load: %s", scene_index + 1, send_pc ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->touchwheel_mode = mode;
    g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
    
    // Re-setup touchwheel instance for new mode
    scene_cleanup_touchwheel();
    scene_setup_touchwheel_for_mode(scene);
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  
  const char* mode_str = (mode == TOUCHWHEEL_MODE_BUTTONS) ? "buttons" :
                         (mode == TOUCHWHEEL_MODE_PROGRAM_CHANGE) ? "program_change" : "continuous";
  ESP_LOGI(TAG, "Scene %d touchwheel mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, uint8_t cc_number, uint8_t value) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || cc_number > 127 || value > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  
  // Create simple CC action
  mapping->actions.num_actions = 1;
  mapping->actions.actions[0] = action_create_send_cc(cc_number, value);
  
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d", 
    scene_index + 1, pad_index, cc_number, value);
  return ESP_OK;
}

esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->touchpads[pad_index].enabled = enabled;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Scene %d pad %d %s", scene_index + 1, pad_index, enabled ? "enabled" : "disabled");
  return ESP_OK;
}

touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS) return NULL;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return NULL;
  
  return &scene->touchpads[pad_index];
}

uint8_t scene_get_pending_index(void) {
  return g_scene_manager.pending_scene_index;
}

bool scene_has_pending_change(void) {
  return g_scene_manager.has_pending_change;
}

esp_err_t scene_confirm_change(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to confirm");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Temporarily switch to immediate mode to perform the change
  scene_change_mode_t old_mode = g_scene_manager.change_mode;
  g_scene_manager.change_mode = CHANGE_MODE_IMMEDIATE;
  g_scene_manager.has_pending_change = false;
  
  esp_err_t ret = scene_set_current(g_scene_manager.pending_scene_index);
  
  // Restore pending mode
  g_scene_manager.change_mode = old_mode;
  
  return ret;
}

esp_err_t scene_cancel_pending(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to cancel");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(TAG, "Cancelled pending scene change to %d", g_scene_manager.pending_scene_index + 1);
  g_scene_manager.has_pending_change = false;
  g_scene_manager.pending_scene_index = g_scene_manager.current_scene_index;
  
  return ESP_OK;
}


esp_err_t scene_save_config(void) {
  ESP_LOGI(TAG, "Saving scene configuration to NVS");
  
  uint8_t mode_val = (uint8_t)g_scene_manager.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  uint8_t change_val = (uint8_t)g_scene_manager.change_mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, change_val);
}

esp_err_t scene_load_config(void) {
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  return ESP_OK;
}

esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed) {
  if (!g_scene_manager.initialized || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = scene_get_current();
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  if (!mapping->enabled) return ESP_OK;  // Pad is disabled
  
  // Handle touchwheel modes - pads 0-7 are routed to touchwheel instance
  // Touchwheel instance handles program change or continuous output
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_BUTTONS && pad_index <= TOUCHWHEEL_END) {
    // Pad events are already routed to touchwheel instance via touch.c
    // Don't process as individual button presses
    return ESP_OK;
  }
  
  // Execute action chain
  ESP_LOGD(TAG, "Pad %d %s: executing %d action(s)", pad_index, 
           pressed ? "pressed" : "released", mapping->actions.num_actions);
  
  return action_execute_chain(&mapping->actions, pressed ? 127 : 0, pressed);
}

esp_err_t scene_assign_touchpad_action(uint8_t scene_index, uint8_t pad_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || !action) {
    return ESP_ERR_INVALID_ARG;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  mapping->actions.num_actions = 1;
  mapping->actions.actions[0] = *action;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Assigned action '%s' to pad %d", action_type_to_string(action->type), pad_index);
  return ESP_OK;
}

esp_err_t scene_assign_touchpad_chain(uint8_t scene_index, uint8_t pad_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || !chain) {
    return ESP_ERR_INVALID_ARG;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  mapping->actions = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Assigned %d actions to pad %d", chain->num_actions, pad_index);
  return ESP_OK;
}

esp_err_t scene_assign_button_left(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_left = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  ESP_LOGI(TAG, "Assigned %d actions to left button", chain->num_actions);
  return ESP_OK;
}

esp_err_t scene_assign_button_right(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_right = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  ESP_LOGI(TAG, "Assigned %d actions to right button", chain->num_actions);
  return ESP_OK;
}

esp_err_t scene_assign_button_both(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_both = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  ESP_LOGI(TAG, "Assigned %d actions to both buttons", chain->num_actions);
  return ESP_OK;
}

action_chain_t* scene_get_button_left(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_left : NULL;
}

action_chain_t* scene_get_button_right(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_right : NULL;
}

action_chain_t* scene_get_button_both(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_both : NULL;
}

esp_err_t scene_assign_on_load(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->on_load = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Assigned %d on_load actions", chain->num_actions);
  return ESP_OK;
}

action_chain_t* scene_get_on_load(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->on_load : NULL;
}

esp_err_t scene_set_expression_mode(uint8_t scene_index, expression_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  // State machine: Cannot change expression mode when in NOTE input mode (except to GATE)
  if (scene->cv_input_mode == INPUT_MODE_NOTE && mode != EXPRESSION_MODE_GATE) {
    ESP_LOGE(TAG, "Cannot change expression mode - locked by NOTE input mode");
    return ESP_ERR_INVALID_STATE;
  }
  
  scene->expression_mode = mode;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // Update hardware configuration immediately if this is the current scene
  expression_set_mode(mode);
  
  const char* mode_str = (mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" : "gate";
  ESP_LOGI(TAG, "Scene %d expression mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

expression_mode_t scene_get_expression_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_mode : EXPRESSION_MODE_PEDAL;
}

esp_err_t scene_assign_sustain(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sustain = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Assigned %d sustain actions", chain->num_actions);
  return ESP_OK;
}

esp_err_t scene_assign_sostenuto(uint8_t scene_index, const action_chain_t* chain) {
  if (scene_index > MAX_SCENE_INDEX || !chain) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sostenuto = *chain;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Assigned %d sostenuto actions", chain->num_actions);
  return ESP_OK;
}

action_chain_t* scene_get_sustain(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sustain : NULL;
}

action_chain_t* scene_get_sostenuto(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sostenuto : NULL;
}

esp_err_t scene_set_cv_input_mode(uint8_t scene_index, input_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  input_mode_t old_mode = scene->cv_input_mode;
  scene->cv_input_mode = mode;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // State machine: NOTE mode requires GATE expression mode
  if (mode == INPUT_MODE_NOTE) {
    // Save current expression mode before changing to GATE
    // (only if we're not already in GATE mode to avoid overwriting the saved mode)
    if (scene->expression_mode != EXPRESSION_MODE_GATE) {
      expression_save_previous_mode(scene->expression_mode);
      ESP_LOGI(TAG, "Saved previous expression mode: %d", scene->expression_mode);
    }
    
    scene->expression_mode = EXPRESSION_MODE_GATE;
    ESP_LOGI(TAG, "Expression mode automatically set to GATE for NOTE input mode");
    
    // Update hardware if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      expression_set_mode(EXPRESSION_MODE_GATE);
    }
  } else if (old_mode == INPUT_MODE_NOTE) {
    // Changing FROM NOTE mode - restore previous expression mode
    expression_mode_t previous_mode = expression_get_previous_mode();
    scene->expression_mode = previous_mode;
    ESP_LOGI(TAG, "Expression mode restored to %d (leaving NOTE mode)", previous_mode);
    
    // Update hardware if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      expression_set_mode(previous_mode);
    }
  }
  
  const char* mode_str = (mode == INPUT_MODE_CV) ? "cv" :
                         (mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                         (mode == INPUT_MODE_AUDIO) ? "audio" : "note";
  ESP_LOGI(TAG, "Scene %d CV input mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

input_mode_t scene_get_cv_input_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_input_mode : INPUT_MODE_CV;
}

esp_err_t scene_set_clock_source(uint8_t scene_index, tempo_clock_source_t source) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->clock_source = source;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // If setting to SYNC, automatically set cv_input_mode to CLOCK_SYNC for coherence
  if (source == CLOCK_SOURCE_SYNC) {
    scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    
    // Switch to clock sync mode if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      input_set_mode(INPUT_MODE_CLOCK_SYNC);
      ESP_LOGI(TAG, "Switched to clock sync input mode");
    }
  }
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    ESP_LOGI(TAG, "Updating tempo to clock source %d (current scene)", source);
    tempo_set_source(source);
  } else {
    ESP_LOGI(TAG, "Not updating tempo (scene %u is not current scene %u)", 
             (unsigned)scene_index, (unsigned)g_scene_manager.current_scene_index);
  }
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Scene %d clock source set to %s", scene_index + 1, source_str);
  
  return ESP_OK;
}

tempo_clock_source_t scene_get_clock_source(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->clock_source : CLOCK_SOURCE_INTERNAL;
}

esp_err_t scene_set_clock_standard(uint8_t scene_index, tempo_clock_standard_t standard) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->clock_standard = standard;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_clock_standard(standard);
  }
  
  const char* std_str = (standard == CLOCK_STANDARD_24PPQN) ? "24PPQN" :
                        (standard == CLOCK_STANDARD_16TH_NOTE) ? "16th Note" : "Beat";
  ESP_LOGI(TAG, "Scene %d clock standard set to %s", scene_index + 1, std_str);
  
  return ESP_OK;
}

tempo_clock_standard_t scene_get_clock_standard(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->clock_standard : CLOCK_STANDARD_24PPQN;
}

esp_err_t scene_set_time_signature(uint8_t scene_index, uint8_t numerator, uint8_t denominator) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (numerator == 0 || numerator > 16 || denominator == 0 || denominator > 16) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->time_signature.numerator = numerator;
  scene->time_signature.denominator = denominator;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_time_signature(numerator, denominator);
  }
  
  ESP_LOGI(TAG, "Scene %d time signature set to %d/%d", scene_index + 1, numerator, denominator);
  
  return ESP_OK;
}

time_signature_t scene_get_time_signature(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  time_signature_t default_sig = {4, 4};
  return scene ? scene->time_signature : default_sig;
}

esp_err_t scene_set_note_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->note_velocity_mode = mode;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  const char* mode_str = (mode == VELOCITY_MODE_FIXED) ? "Fixed" : "Gate Voltage";
  ESP_LOGI(TAG, "Scene %d NOTE velocity mode set to %s", scene_index + 1, mode_str);
  
  return ESP_OK;
}

velocity_mode_t scene_get_note_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->note_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_note_fixed_velocity(uint8_t scene_index, uint8_t velocity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (velocity < 1 || velocity > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->note_fixed_velocity = velocity;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Scene %d NOTE fixed velocity set to %d", scene_index + 1, velocity);
  
  return ESP_OK;
}

uint8_t scene_get_note_fixed_velocity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->note_fixed_velocity : 100;
}

// Helper to get scene filename
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size) {
  snprintf(buffer, buffer_size, "%s/scene_%03d.json", SCENES_BASE_PATH, scene_index + 1);
}

// Action type name lookup table
static const char* action_type_json_names[] = {
  [ACTION_NONE] = "none",
  [ACTION_PROGRAM_NEXT] = "program_next",
  [ACTION_PROGRAM_PREV] = "program_prev",
  [ACTION_PROGRAM_SET] = "program_set",
  [ACTION_PROGRAM_BANK_SET] = "program_bank_set",
  [ACTION_SCENE_NEXT] = "scene_next",
  [ACTION_SCENE_PREV] = "scene_prev",
  [ACTION_SCENE_SET] = "scene_set",
  [ACTION_TRANSPORT_PLAY] = "transport_play",
  [ACTION_TRANSPORT_STOP] = "transport_stop",
  [ACTION_TRANSPORT_PAUSE] = "transport_pause",
  [ACTION_TRANSPORT_RECORD] = "transport_record",
  [ACTION_TRANSPORT_TOGGLE] = "transport_toggle",
  [ACTION_TAP_TEMPO] = "tap_tempo",
  [ACTION_TEMPO_NUDGE_UP] = "tempo_nudge_up",
  [ACTION_TEMPO_NUDGE_DOWN] = "tempo_nudge_down",
  [ACTION_SEND_CC] = "send_cc",
  [ACTION_SEND_CC_TOGGLE] = "send_cc_toggle",
  [ACTION_SEND_CC_CYCLE] = "send_cc_cycle",
  [ACTION_SEND_DOUBLE_CC] = "send_double_cc",
  [ACTION_SEND_NRPN] = "send_nrpn",
  [ACTION_SEND_RPN] = "send_rpn",
  [ACTION_SEND_NOTE_ON] = "send_note_on",
  [ACTION_SEND_NOTE_OFF] = "send_note_off",
  [ACTION_SEND_PC] = "send_pc",
  [ACTION_SEND_PITCH_BEND] = "send_pitch_bend",
  [ACTION_SEND_AFTERTOUCH] = "send_aftertouch",
  [ACTION_SEND_POLY_AFTERTOUCH] = "send_poly_aftertouch",
  [ACTION_SEND_SONG_SELECT] = "send_song_select",
  [ACTION_SEND_SONG_POSITION] = "send_song_position",
  [ACTION_SEND_MMC] = "send_mmc",
  [ACTION_RANDOMIZE_CC] = "randomize_cc",
  [ACTION_RANDOMIZE_MULTI] = "randomize_multi",
  [ACTION_SEND_CLOCK_START] = "send_clock_start",
  [ACTION_SEND_CLOCK_STOP] = "send_clock_stop",
  [ACTION_SEND_CLOCK_CONTINUE] = "send_clock_continue",
  [ACTION_SEND_RESET] = "send_reset",
  [ACTION_SEND_TUNE_REQUEST] = "send_tune_request",
  [ACTION_SCREENSAVER_TOGGLE] = "screensaver_toggle",
  [ACTION_CONFIRM_PENDING] = "confirm_pending",
  [ACTION_CANCEL_PENDING] = "cancel_pending",
  [ACTION_ALL_NOTES_OFF] = "all_notes_off",
  [ACTION_ALL_SOUND_OFF] = "all_sound_off",
  [ACTION_SUSTAIN] = "sustain",
  [ACTION_SOSTENUTO] = "sostenuto"
};

// Helper to convert action type string to enum
static action_type_t action_type_from_string(const char* name) {
  if (!name) return ACTION_NONE;
  
  for (int i = 0; i < ACTION_MAX; i++) {
    if (action_type_json_names[i] && strcmp(name, action_type_json_names[i]) == 0) {
      return (action_type_t)i;
    }
  }
  
  return ACTION_NONE;
}

// Serialize/deserialize actions
static cJSON* action_to_json(const action_t* action) {
  cJSON* obj = cJSON_CreateObject();
  
  // Use string name instead of integer
  if (action->type < ACTION_MAX && action_type_json_names[action->type]) {
    cJSON_AddStringToObject(obj, "type", action_type_json_names[action->type]);
  } else {
    cJSON_AddStringToObject(obj, "type", "none");
  }
  
  if (action->type == ACTION_SEND_CC || action->type == ACTION_SEND_CC_TOGGLE || action->type == ACTION_RANDOMIZE_CC) {
    cJSON_AddNumberToObject(obj, "cc", action->params.cc.cc_number);
    cJSON_AddNumberToObject(obj, "value", action->params.cc.value);
    if (action->type == ACTION_SEND_CC_TOGGLE) cJSON_AddNumberToObject(obj, "value2", action->params.cc.value2);
  } else if (action->type == ACTION_SEND_NOTE_ON || action->type == ACTION_SEND_NOTE_OFF) {
    cJSON_AddNumberToObject(obj, "note", action->params.note.note);
    cJSON_AddNumberToObject(obj, "velocity", action->params.note.velocity);
  } else if (action->type == ACTION_PROGRAM_SET || action->type == ACTION_SCENE_SET || action->type == ACTION_SEND_PC) {
    cJSON_AddNumberToObject(obj, "number", action->params.target.number);
  }
  
  return obj;
}

static action_t json_to_action(cJSON* obj) {
  action_t action = {0};
  cJSON* type = cJSON_GetObjectItem(obj, "type");
  
  if (type) {
    if (cJSON_IsString(type)) {
      // New format: string name
      action.type = action_type_from_string(type->valuestring);
      ESP_LOGI(TAG, "Loaded action: %s -> %d", type->valuestring, action.type);
    } else if (cJSON_IsNumber(type)) {
      // Legacy format: integer (for backward compatibility)
      action.type = (action_type_t)type->valueint;
      ESP_LOGW(TAG, "Loading action with legacy integer type %d (use string names)", type->valueint);
    } else {
      ESP_LOGE(TAG, "Action type is neither string nor number!");
    }
  } else {
    ESP_LOGE(TAG, "Action missing 'type' field!");
  }
  
  cJSON* cc = cJSON_GetObjectItem(obj, "cc");
  cJSON* value = cJSON_GetObjectItem(obj, "value");
  cJSON* value2 = cJSON_GetObjectItem(obj, "value2");
  cJSON* note = cJSON_GetObjectItem(obj, "note");
  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  cJSON* number = cJSON_GetObjectItem(obj, "number");
  
  if (cc) action.params.cc.cc_number = cc->valueint;
  if (value) action.params.cc.value = value->valueint;
  if (value2) action.params.cc.value2 = value2->valueint;
  if (note) action.params.note.note = note->valueint;
  if (velocity) action.params.note.velocity = velocity->valueint;
  if (number) action.params.target.number = number->valueint;
  
  return action;
}

static cJSON* action_chain_to_json(const action_chain_t* chain) {
  cJSON* array = cJSON_CreateArray();
  for (int i = 0; i < chain->num_actions; i++) {
    cJSON_AddItemToArray(array, action_to_json(&chain->actions[i]));
  }
  return array;
}

static action_chain_t json_to_action_chain(cJSON* array) {
  action_chain_t chain = {0};
  if (!cJSON_IsArray(array)) return chain;
  
  int count = cJSON_GetArraySize(array);
  chain.num_actions = (count > MAX_ACTIONS_PER_INPUT) ? MAX_ACTIONS_PER_INPUT : count;
  
  for (int i = 0; i < chain.num_actions; i++) {
    chain.actions[i] = json_to_action(cJSON_GetArrayItem(array, i));
  }
  return chain;
}

// Serialize continuous mapping to JSON
static cJSON* continuous_mapping_to_json(const continuous_mapping_t* mapping) {
  cJSON* obj = cJSON_CreateObject();
  
  cJSON_AddBoolToObject(obj, "enabled", mapping->enabled);
  cJSON_AddStringToObject(obj, "output_type", mapping->output_type == OUTPUT_TYPE_NOTE ? "note" : "cc");
  cJSON_AddNumberToObject(obj, "cc_number", mapping->cc_number);
  cJSON_AddNumberToObject(obj, "base_note", mapping->base_note);
  cJSON_AddNumberToObject(obj, "note_range", mapping->note_range);
  cJSON_AddNumberToObject(obj, "velocity", mapping->velocity);
  cJSON_AddNumberToObject(obj, "curve_type", mapping->curve.type);
  cJSON_AddNumberToObject(obj, "polarity", mapping->polarity);
  cJSON_AddNumberToObject(obj, "min_value", mapping->min_value);
  cJSON_AddNumberToObject(obj, "max_value", mapping->max_value);
  cJSON_AddBoolToObject(obj, "use_idle_value", mapping->use_idle_value);
  cJSON_AddNumberToObject(obj, "idle_value", mapping->idle_value);
  cJSON_AddNumberToObject(obj, "idle_timeout_ms", mapping->idle_timeout_ms);
  
  return obj;
}

// Deserialize continuous mapping from JSON
static void json_to_continuous_mapping(cJSON* obj, continuous_mapping_t* mapping) {
  if (!obj || !mapping) return;
  
  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) mapping->enabled = cJSON_IsTrue(enabled);
  
  cJSON* output_type = cJSON_GetObjectItem(obj, "output_type");
  if (output_type && cJSON_IsString(output_type)) {
    mapping->output_type = (strcmp(output_type->valuestring, "note") == 0) ? OUTPUT_TYPE_NOTE : OUTPUT_TYPE_CC;
  }
  
  cJSON* cc_num = cJSON_GetObjectItem(obj, "cc_number");
  if (cc_num) mapping->cc_number = cc_num->valueint;
  
  cJSON* base_note = cJSON_GetObjectItem(obj, "base_note");
  if (base_note) mapping->base_note = base_note->valueint;
  
  cJSON* note_range = cJSON_GetObjectItem(obj, "note_range");
  if (note_range) mapping->note_range = note_range->valueint;
  
  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  if (velocity) mapping->velocity = velocity->valueint;
  
  cJSON* curve_type = cJSON_GetObjectItem(obj, "curve_type");
  if (curve_type) mapping->curve = curve_create((curve_type_t)curve_type->valueint);
  
  cJSON* polarity = cJSON_GetObjectItem(obj, "polarity");
  if (polarity) mapping->polarity = (polarity_t)polarity->valueint;
  
  cJSON* min_val = cJSON_GetObjectItem(obj, "min_value");
  if (min_val) mapping->min_value = min_val->valueint;
  
  cJSON* max_val = cJSON_GetObjectItem(obj, "max_value");
  if (max_val) mapping->max_value = max_val->valueint;
  
  cJSON* use_idle = cJSON_GetObjectItem(obj, "use_idle_value");
  if (use_idle) mapping->use_idle_value = cJSON_IsTrue(use_idle);
  
  cJSON* idle_val = cJSON_GetObjectItem(obj, "idle_value");
  if (idle_val) mapping->idle_value = idle_val->valueint;
  
  cJSON* idle_timeout = cJSON_GetObjectItem(obj, "idle_timeout_ms");
  if (idle_timeout) mapping->idle_timeout_ms = idle_timeout->valueint;
}

// Scene JSON serialization
static cJSON* scene_to_json(const scene_t* scene) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", scene->name);
  cJSON_AddNumberToObject(root, "program_number", scene->program_number);
  cJSON_AddBoolToObject(root, "send_pc_on_load", scene->send_pc_on_load);
  
  // Serialize touchwheel mode, style, and continuous mapping
  const char* tw_mode_str = (scene->touchwheel_mode == TOUCHWHEEL_MODE_BUTTONS) ? "buttons" :
                            (scene->touchwheel_mode == TOUCHWHEEL_MODE_PROGRAM_CHANGE) ? "program_change" : "continuous";
  cJSON_AddStringToObject(root, "touchwheel_mode", tw_mode_str);
  const char* tw_style_str = (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? "endless" : "odometer";
  cJSON_AddStringToObject(root, "touchwheel_style", tw_style_str);
  cJSON_AddItemToObject(root, "touchwheel", continuous_mapping_to_json(&scene->touchwheel));
  
  cJSON* touchpads = cJSON_CreateArray();
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    cJSON* pad = cJSON_CreateObject();
    cJSON_AddBoolToObject(pad, "enabled", scene->touchpads[i].enabled);
    cJSON_AddItemToObject(pad, "actions", action_chain_to_json(&scene->touchpads[i].actions));
    cJSON_AddItemToArray(touchpads, pad);
  }
  cJSON_AddItemToObject(root, "touchpads", touchpads);
  
  cJSON_AddItemToObject(root, "on_load", action_chain_to_json(&scene->on_load));
  cJSON_AddItemToObject(root, "button_left", action_chain_to_json(&scene->button_left));
  cJSON_AddItemToObject(root, "button_right", action_chain_to_json(&scene->button_right));
  cJSON_AddItemToObject(root, "button_both", action_chain_to_json(&scene->button_both));
  
  // Serialize continuous mappings
  cJSON_AddItemToObject(root, "expression", continuous_mapping_to_json(&scene->expression));
  cJSON_AddItemToObject(root, "cv", continuous_mapping_to_json(&scene->cv));
  cJSON_AddItemToObject(root, "proximity", continuous_mapping_to_json(&scene->proximity));
  cJSON_AddItemToObject(root, "als", continuous_mapping_to_json(&scene->als));
  
  // Serialize expression jack mode and pedal actions
  const char* mode_str = (scene->expression_mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" : "gate";
  cJSON_AddStringToObject(root, "expression_mode", mode_str);
  cJSON_AddItemToObject(root, "sustain", action_chain_to_json(&scene->sustain));
  cJSON_AddItemToObject(root, "sostenuto", action_chain_to_json(&scene->sostenuto));
  
  // Serialize CV input mode
  const char* cv_mode_str = (scene->cv_input_mode == INPUT_MODE_CV) ? "cv" :
                            (scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                            (scene->cv_input_mode == INPUT_MODE_AUDIO) ? "audio" : "note";
  cJSON_AddStringToObject(root, "cv_input_mode", cv_mode_str);
  
  // Serialize NOTE mode velocity settings
  const char* vel_mode_str = (scene->note_velocity_mode == VELOCITY_MODE_FIXED) ? "fixed" : "gate_voltage";
  cJSON_AddStringToObject(root, "note_velocity_mode", vel_mode_str);
  cJSON_AddNumberToObject(root, "note_fixed_velocity", scene->note_fixed_velocity);
  
  // Serialize tempo settings
  const char* clock_src_str = (scene->clock_source == CLOCK_SOURCE_INTERNAL) ? "internal" :
                              (scene->clock_source == CLOCK_SOURCE_MIDI) ? "midi" : "sync";
  cJSON_AddStringToObject(root, "clock_source", clock_src_str);
  
  const char* clock_std_str = (scene->clock_standard == CLOCK_STANDARD_24PPQN) ? "24ppqn" :
                              (scene->clock_standard == CLOCK_STANDARD_16TH_NOTE) ? "16th_note" : "beat";
  cJSON_AddStringToObject(root, "clock_standard", clock_std_str);
  
  cJSON* time_sig = cJSON_CreateObject();
  cJSON_AddNumberToObject(time_sig, "numerator", scene->time_signature.numerator);
  cJSON_AddNumberToObject(time_sig, "denominator", scene->time_signature.denominator);
  cJSON_AddItemToObject(root, "time_signature", time_sig);
  
  return root;
}

static esp_err_t json_to_scene(cJSON* root, scene_t* scene) {
  if (!root || !scene) return ESP_ERR_INVALID_ARG;
  
  cJSON* name = cJSON_GetObjectItem(root, "name");
  if (name && cJSON_IsString(name)) {
    strncpy(scene->name, name->valuestring, sizeof(scene->name) - 1);
    scene->name[sizeof(scene->name) - 1] = '\0';
  }
  
  cJSON* program = cJSON_GetObjectItem(root, "program_number");
  if (program) scene->program_number = program->valueint;
  
  // Support both old and new key names for backward compatibility
  cJSON* send_pc = cJSON_GetObjectItem(root, "send_pc_on_load");
  if (!send_pc) send_pc = cJSON_GetObjectItem(root, "send_pc_on_change");  // Legacy
  if (send_pc) scene->send_pc_on_load = cJSON_IsTrue(send_pc);
  
  // Deserialize touchwheel mode
  cJSON* tw_mode = cJSON_GetObjectItem(root, "touchwheel_mode");
  if (tw_mode && cJSON_IsString(tw_mode)) {
    const char* mode_str = tw_mode->valuestring;
    if (strcmp(mode_str, "program_change") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
    else if (strcmp(mode_str, "continuous") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_CONTINUOUS;
    else scene->touchwheel_mode = TOUCHWHEEL_MODE_BUTTONS;
  }
  
  // Deserialize touchwheel style (odometer vs endless)
  cJSON* tw_style = cJSON_GetObjectItem(root, "touchwheel_style");
  if (tw_style && cJSON_IsString(tw_style)) {
    const char* style_str = tw_style->valuestring;
    if (strcmp(style_str, "endless") == 0) scene->touchwheel_style = TOUCHWHEEL_STYLE_ENDLESS;
    else scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;
  }
  
  // Deserialize touchwheel continuous mapping
  cJSON* touchwheel = cJSON_GetObjectItem(root, "touchwheel");
  if (touchwheel) json_to_continuous_mapping(touchwheel, &scene->touchwheel);
  
  cJSON* touchpads = cJSON_GetObjectItem(root, "touchpads");
  if (touchpads && cJSON_IsArray(touchpads)) {
    int count = cJSON_GetArraySize(touchpads);
    for (int i = 0; i < count && i < NUM_TOUCHPADS; i++) {
      cJSON* pad = cJSON_GetArrayItem(touchpads, i);
      cJSON* enabled = cJSON_GetObjectItem(pad, "enabled");
      if (enabled) scene->touchpads[i].enabled = cJSON_IsTrue(enabled);
      
      cJSON* actions = cJSON_GetObjectItem(pad, "actions");
      if (actions) scene->touchpads[i].actions = json_to_action_chain(actions);
    }
  }
  
  cJSON* on_load = cJSON_GetObjectItem(root, "on_load");
  if (on_load) scene->on_load = json_to_action_chain(on_load);
  
  cJSON* btn_l = cJSON_GetObjectItem(root, "button_left");
  if (btn_l) scene->button_left = json_to_action_chain(btn_l);
  
  cJSON* btn_r = cJSON_GetObjectItem(root, "button_right");
  if (btn_r) scene->button_right = json_to_action_chain(btn_r);
  
  cJSON* btn_both = cJSON_GetObjectItem(root, "button_both");
  if (btn_both) scene->button_both = json_to_action_chain(btn_both);
  
  // Deserialize continuous mappings
  cJSON* expression = cJSON_GetObjectItem(root, "expression");
  if (expression) json_to_continuous_mapping(expression, &scene->expression);
  
  cJSON* cv = cJSON_GetObjectItem(root, "cv");
  if (cv) json_to_continuous_mapping(cv, &scene->cv);
  
  cJSON* proximity = cJSON_GetObjectItem(root, "proximity");
  if (proximity) json_to_continuous_mapping(proximity, &scene->proximity);
  
  cJSON* als = cJSON_GetObjectItem(root, "als");
  if (als) json_to_continuous_mapping(als, &scene->als);
  
  // Deserialize expression jack mode
  cJSON* expr_mode = cJSON_GetObjectItem(root, "expression_mode");
  if (expr_mode && cJSON_IsString(expr_mode)) {
    const char* mode_str = expr_mode->valuestring;
    if (strcmp(mode_str, "sustain") == 0) scene->expression_mode = EXPRESSION_MODE_SUSTAIN;
    else if (strcmp(mode_str, "sostenuto") == 0) scene->expression_mode = EXPRESSION_MODE_SOSTENUTO;
    else if (strcmp(mode_str, "gate") == 0) scene->expression_mode = EXPRESSION_MODE_GATE;
    else scene->expression_mode = EXPRESSION_MODE_PEDAL;
  }
  
  // Deserialize pedal actions
  cJSON* sustain = cJSON_GetObjectItem(root, "sustain");
  if (sustain) scene->sustain = json_to_action_chain(sustain);
  
  cJSON* sostenuto = cJSON_GetObjectItem(root, "sostenuto");
  if (sostenuto) scene->sostenuto = json_to_action_chain(sostenuto);
  
  // Deserialize CV input mode
  cJSON* cv_mode = cJSON_GetObjectItem(root, "cv_input_mode");
  if (cv_mode && cJSON_IsString(cv_mode)) {
    const char* mode_str = cv_mode->valuestring;
    if (strcmp(mode_str, "clock_sync") == 0) scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    else if (strcmp(mode_str, "audio") == 0) scene->cv_input_mode = INPUT_MODE_AUDIO;
    else if (strcmp(mode_str, "note") == 0) scene->cv_input_mode = INPUT_MODE_NOTE;
    else scene->cv_input_mode = INPUT_MODE_CV;
  }
  
  // Deserialize NOTE mode velocity settings
  cJSON* vel_mode = cJSON_GetObjectItem(root, "note_velocity_mode");
  if (vel_mode && cJSON_IsString(vel_mode)) {
    const char* mode_str = vel_mode->valuestring;
    if (strcmp(mode_str, "gate_voltage") == 0) scene->note_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
    else scene->note_velocity_mode = VELOCITY_MODE_FIXED;
  }
  
  cJSON* fixed_vel = cJSON_GetObjectItem(root, "note_fixed_velocity");
  if (fixed_vel) {
    int vel = fixed_vel->valueint;
    if (vel >= 1 && vel <= 127) scene->note_fixed_velocity = (uint8_t)vel;
  }
  
  // Deserialize tempo settings
  cJSON* clock_src = cJSON_GetObjectItem(root, "clock_source");
  if (clock_src && cJSON_IsString(clock_src)) {
    const char* src_str = clock_src->valuestring;
    if (strcmp(src_str, "midi") == 0) scene->clock_source = CLOCK_SOURCE_MIDI;
    else if (strcmp(src_str, "sync") == 0) scene->clock_source = CLOCK_SOURCE_SYNC;
    else scene->clock_source = CLOCK_SOURCE_INTERNAL;
  }
  
  cJSON* clock_std = cJSON_GetObjectItem(root, "clock_standard");
  if (clock_std && cJSON_IsString(clock_std)) {
    const char* std_str = clock_std->valuestring;
    if (strcmp(std_str, "16th_note") == 0) scene->clock_standard = CLOCK_STANDARD_16TH_NOTE;
    else if (strcmp(std_str, "beat") == 0) scene->clock_standard = CLOCK_STANDARD_BEAT;
    else scene->clock_standard = CLOCK_STANDARD_24PPQN;
  }
  
  cJSON* time_sig = cJSON_GetObjectItem(root, "time_signature");
  if (time_sig && cJSON_IsObject(time_sig)) {
    cJSON* numerator = cJSON_GetObjectItem(time_sig, "numerator");
    cJSON* denominator = cJSON_GetObjectItem(time_sig, "denominator");
    if (numerator) scene->time_signature.numerator = numerator->valueint;
    if (denominator) scene->time_signature.denominator = denominator->valueint;
  }

  return ESP_OK;
}

// Scene storage implementation
esp_err_t scene_load_from_flash(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;
  
  // Load into cache
  int cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  esp_err_t ret = json_to_scene(root, &g_scene_manager.cache[cache_idx].scene);
  cJSON_Delete(root);
  
  if (ret == ESP_OK) {
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
    g_scene_manager.cache[cache_idx].dirty = false;
  }
  
  return ret;
}

esp_err_t scene_save_to_flash(uint8_t scene_index) {
  scene_t* scene = NULL;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      scene = &g_scene_manager.cache[i].scene;
      break;
    }
  }
  if (!scene) return ESP_ERR_NOT_FOUND;
  
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  cJSON* root = scene_to_json(scene);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(filepath, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Saved scene %d to flash", scene_index);
  return ESP_OK;
}

esp_err_t scene_load_manifest(void) {
  FILE* f = fopen(MANIFEST_PATH, "r");
  if (!f) return ESP_ERR_NOT_FOUND;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;
  
  cJSON* scenes = cJSON_GetObjectItem(root, "scenes");
  if (!scenes || !cJSON_IsArray(scenes)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
  
  int count = cJSON_GetArraySize(scenes);
  g_scene_manager.manifest = malloc(count * sizeof(scene_manifest_entry_t));
  if (!g_scene_manager.manifest) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
  
  g_scene_manager.num_scenes = count;
  for (int i = 0; i < count; i++) {
    cJSON* entry = cJSON_GetArrayItem(scenes, i);
    cJSON* idx = cJSON_GetObjectItem(entry, "index");
    cJSON* name = cJSON_GetObjectItem(entry, "name");
    cJSON* filename = cJSON_GetObjectItem(entry, "filename");
    
    if (idx) g_scene_manager.manifest[i].index = idx->valueint;
    if (name && cJSON_IsString(name)) {
      strncpy(g_scene_manager.manifest[i].name, name->valuestring, 31);
      g_scene_manager.manifest[i].name[31] = '\0';
    }
    if (filename && cJSON_IsString(filename)) {
      strncpy(g_scene_manager.manifest[i].filename, filename->valuestring, 63);
      g_scene_manager.manifest[i].filename[63] = '\0';
    }
  }
  
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t scene_save_manifest(void) {
  cJSON* root = cJSON_CreateObject();
  cJSON* scenes = cJSON_CreateArray();
  
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "index", g_scene_manager.manifest[i].index);
    cJSON_AddStringToObject(entry, "name", g_scene_manager.manifest[i].name);
    cJSON_AddStringToObject(entry, "filename", g_scene_manager.manifest[i].filename);
    cJSON_AddItemToArray(scenes, entry);
  }
  
  cJSON_AddItemToObject(root, "scenes", scenes);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(MANIFEST_PATH, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  return ESP_OK;
}

uint16_t scene_get_count(void) {
  return g_scene_manager.num_scenes;
}

esp_err_t scene_create_new(const char* name) {
  // Find next available index
  uint8_t new_index = 0;
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    bool exists = false;
    for (int j = 0; j < g_scene_manager.num_scenes; j++) {
      if (g_scene_manager.manifest[j].index == i) { exists = true; break; }
    }
    if (!exists) { new_index = i; break; }
  }
  
  // Expand manifest
  scene_manifest_entry_t* new_manifest = realloc(g_scene_manager.manifest, 
                                                  (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
  if (!new_manifest) return ESP_ERR_NO_MEM;
  
  g_scene_manager.manifest = new_manifest;
  g_scene_manager.manifest[g_scene_manager.num_scenes].index = new_index;
  strncpy(g_scene_manager.manifest[g_scene_manager.num_scenes].name, name, 31);
  snprintf(g_scene_manager.manifest[g_scene_manager.num_scenes].filename, 63, "scene_%03d.json", new_index + 1);
  g_scene_manager.num_scenes++;
  
  // Create and save default scene
  scene_t new_scene;
  scene_init_defaults(&new_scene, new_index);
  strncpy(new_scene.name, name, 31);
  
  int temp_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  g_scene_manager.cache[temp_idx].scene = new_scene;
  g_scene_manager.cache[temp_idx].index = new_index;
  g_scene_manager.cache[temp_idx].valid = true;
  
  scene_save_to_flash(new_index);
  scene_save_manifest();
  
  return ESP_OK;
}

esp_err_t scene_delete(uint8_t scene_index) {
  if (g_scene_manager.num_scenes == 1) return ESP_ERR_INVALID_STATE;
  
  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) { pos = i; break; }
  }
  if (pos == -1) return ESP_ERR_NOT_FOUND;
  
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  remove(filepath);
  
  for (int i = pos; i < g_scene_manager.num_scenes - 1; i++) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  }
  g_scene_manager.num_scenes--;
  
  scene_save_manifest();
  return ESP_OK;
}

esp_err_t scene_duplicate(uint8_t source_index, const char* new_name) {
  return scene_create_new(new_name);  // Simplified for now
}

esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index) {
  int from_pos = -1, to_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == from_index) from_pos = i;
    if (g_scene_manager.manifest[i].index == to_index) to_pos = i;
  }
  if (from_pos == -1 || to_pos == -1) return ESP_ERR_INVALID_ARG;
  
  scene_manifest_entry_t temp = g_scene_manager.manifest[from_pos];
  
  if (from_pos < to_pos) {
    for (int i = from_pos; i < to_pos; i++) g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  } else {
    for (int i = from_pos; i > to_pos; i--) g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }
  
  g_scene_manager.manifest[to_pos] = temp;
  scene_save_manifest();
  
  return ESP_OK;
}
