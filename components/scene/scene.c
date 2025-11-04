#include "scene.h"
#include "esp_log.h"
#include "midi_messages.h"
#include "device_config.h"
#include "app_settings.h"
#include "event_bus.h"
#include "action.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "scene";

// NVS keys
#define NVS_KEY_SCENE_MODE       "scene_mode"
#define NVS_KEY_CHANGE_MODE      "change_mode"

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
  .initialized = false
};

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
  scene->send_pc_on_change = true;
  
  // Default touchwheel mode
  scene->touchwheel_mode = TOUCHWHEEL_MODE_BUTTONS;
  scene->touchwheel_actions.num_actions = 1;
  scene->touchwheel_actions.actions[0] = action_create_send_cc(16, 64);  // GP Controller 1
  
  // Initialize touchpad mappings with default CC actions
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    scene->touchpads[i].actions.num_actions = 1;
    scene->touchpads[i].actions.actions[0] = action_create_send_cc(DEFAULT_CC_NUMBERS[i], 127);
  }
  
  // Set default button assignments
  set_default_button_assignments(scene);
  
  // Initialize discrete trigger inputs
  scene->bump.num_actions = 1;
  scene->bump.actions[0] = action_create_tap_tempo();  // Default: tap tempo on bump
  
  // Initialize continuous input mappings with defaults
  scene->expression = continuous_mapping_create(11);   // CC11 = Expression
  scene->cv = continuous_mapping_create(16);           // CC16 = General Purpose 1
  scene->proximity = continuous_mapping_create(17);    // CC17 = General Purpose 2
  scene->proximity.use_idle_value = true;              // Proximity returns to center
  scene->proximity.idle_value = 64;
  scene->proximity.idle_timeout_ms = 1000;
  scene->proximity.polarity = POLARITY_BIPOLAR;
  scene->als = continuous_mapping_create(18);          // CC18 = General Purpose 3
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
  
  // Load first scene into cache
  g_scene_manager.current_cache_idx = 0;
  g_scene_manager.current_scene_index = 0;
  
  // Try to load from flash, or create default
  ret = scene_load_from_flash(0);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load scene 0 from flash, using defaults");
    scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
  }
  g_scene_manager.cache[0].index = 0;
  g_scene_manager.cache[0].valid = true;
  g_scene_manager.cache[0].dirty = false;
  
  g_scene_manager.initialized = true;
  
  const char* mode_str = (g_scene_manager.mode == SCENE_MODE_SINGLE) ? "Single" :
                         (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene manager initialized: mode=%s, total_scenes=%d", mode_str, g_scene_manager.num_scenes);
  
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
    
    // Save current cache entry if dirty
    if (g_scene_manager.cache[cache_idx].valid && g_scene_manager.cache[cache_idx].dirty) {
      scene_save_to_flash(g_scene_manager.cache[cache_idx].index);
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
  
  // Send program change based on mode
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC && new_scene->send_pc_on_change) {
    uint8_t channel = device_config_get_channel();
    send_program_change(channel - 1, scene_index);
    ESP_LOGD(TAG, "Sent PC %d on channel %d (preset sync)", scene_index, channel);
  } else if (g_scene_manager.mode == SCENE_MODE_ADVANCED && new_scene->send_pc_on_change) {
    uint8_t channel = device_config_get_channel();
    send_program_change(channel - 1, new_scene->program_number);
    ESP_LOGD(TAG, "Sent PC %d on channel %d", new_scene->program_number, channel);
  }
  
  ESP_LOGI(TAG, "Switched to scene %d: %s", scene_index + 1, new_scene->name);
  
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

esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program) {
  if (scene_index > MAX_SCENE_INDEX || program > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->program_number = program;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  ESP_LOGI(TAG, "Scene %d program number set to %d", scene_index + 1, program);
  return ESP_OK;
}

esp_err_t scene_set_send_pc(uint8_t scene_index, bool send_pc) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  // Get current scene if it matches
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->send_pc_on_change = send_pc;
    g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  } else {
    ESP_LOGW(TAG, "Can only modify current scene (load scene %d first)", scene_index);
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Scene %d send PC on change: %s", scene_index + 1, send_pc ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->touchwheel_mode = mode;
    g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Scene %d touchwheel mode set to %s", scene_index + 1, mode == TOUCHWHEEL_MODE_BUTTONS ? "buttons" : "encoder");
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
  
  // Handle touchwheel encoder mode
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_ENCODER && pad_index <= TOUCHWHEEL_END) {
    // TODO: Implement encoder logic with previous position tracking
    ESP_LOGD(TAG, "Touchwheel encoder mode not yet implemented");
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

#define SCENES_BASE_PATH "/assets/scenes"
#define MANIFEST_PATH "/assets/scenes/manifest.json"

// Helper to get scene filename
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size) {
  snprintf(buffer, buffer_size, "%s/scene_%03d.json", SCENES_BASE_PATH, scene_index + 1);
}

// Serialize/deserialize actions
static cJSON* action_to_json(const action_t* action) {
  cJSON* obj = cJSON_CreateObject();
  cJSON_AddNumberToObject(obj, "type", action->type);
  
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
  if (type) action.type = (action_type_t)type->valueint;
  
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

// Scene JSON serialization
static cJSON* scene_to_json(const scene_t* scene) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", scene->name);
  cJSON_AddNumberToObject(root, "program_number", scene->program_number);
  cJSON_AddBoolToObject(root, "send_pc_on_change", scene->send_pc_on_change);
  
  cJSON* touchpads = cJSON_CreateArray();
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    cJSON* pad = cJSON_CreateObject();
    cJSON_AddBoolToObject(pad, "enabled", scene->touchpads[i].enabled);
    cJSON_AddItemToObject(pad, "actions", action_chain_to_json(&scene->touchpads[i].actions));
    cJSON_AddItemToArray(touchpads, pad);
  }
  cJSON_AddItemToObject(root, "touchpads", touchpads);
  
  cJSON_AddItemToObject(root, "button_left", action_chain_to_json(&scene->button_left));
  cJSON_AddItemToObject(root, "button_right", action_chain_to_json(&scene->button_right));
  cJSON_AddItemToObject(root, "button_both", action_chain_to_json(&scene->button_both));
  
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
  
  cJSON* send_pc = cJSON_GetObjectItem(root, "send_pc_on_change");
  if (send_pc) scene->send_pc_on_change = cJSON_IsTrue(send_pc);
  
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
  
  cJSON* btn_l = cJSON_GetObjectItem(root, "button_left");
  if (btn_l) scene->button_left = json_to_action_chain(btn_l);
  
  cJSON* btn_r = cJSON_GetObjectItem(root, "button_right");
  if (btn_r) scene->button_right = json_to_action_chain(btn_r);
  
  cJSON* btn_both = cJSON_GetObjectItem(root, "button_both");
  if (btn_both) scene->button_both = json_to_action_chain(btn_both);
  
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
