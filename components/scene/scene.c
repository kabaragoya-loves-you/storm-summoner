#include "scene.h"
#include "esp_log.h"
#include "midi_messages.h"
#include "device_config.h"
#include "app_settings.h"
#include "event_bus.h"
#include "action.h"
#include <string.h>

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
  scene->touchwheel_cc.cc_number = 16;  // General Purpose Controller 1
  scene->touchwheel_cc.value = 0;
  scene->touchwheel_cc.channel = 0;
  
  // Initialize touchpad mappings with default CC actions
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    
    // Initialize with simple CC action
    scene->touchpads[i].actions.num_actions = 1;
    scene->touchpads[i].actions.actions[0] = action_create_send_cc(DEFAULT_CC_NUMBERS[i], 127);
    
    // Keep legacy CC field for compatibility
    scene->touchpads[i].cc.cc_number = DEFAULT_CC_NUMBERS[i];
    scene->touchpads[i].cc.value = 127;
    scene->touchpads[i].cc.channel = 0;
  }
  
  // Set default button assignments
  set_default_button_assignments(scene);
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

uint16_t scene_get_count(void) {
  return g_scene_manager.num_scenes;
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

esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, uint8_t cc_number, uint8_t value, uint8_t channel) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || cc_number > 127 || value > 127 || channel > 16) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  mapping->cc.cc_number = cc_number;
  mapping->cc.value = value;
  mapping->cc.channel = channel;
  g_scene_manager.cache[g_scene_manager.current_cache_idx].dirty = true;
  
  uint8_t effective_ch = channel ? channel : device_config_get_channel();
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d ch %d", 
    scene_index + 1, pad_index, cc_number, value, effective_ch);
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

uint8_t scene_get_effective_channel(const touchpad_mapping_t* mapping, const scene_t* scene) {
  if (!mapping) return device_config_get_channel();
  
  // If mapping has explicit channel, use it; otherwise use device config channel
  return (mapping->cc.channel > 0) ? mapping->cc.channel : device_config_get_channel();
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

// Scene storage functions (stub implementations for now)
esp_err_t scene_load_from_flash(uint8_t scene_index) {
  ESP_LOGD(TAG, "Loading scene %d from flash (not yet implemented)", scene_index);
  // TODO: Implement JSON loading from /assets/scenes/scene_XXX.json
  return ESP_ERR_NOT_FOUND;
}

esp_err_t scene_save_to_flash(uint8_t scene_index) {
  ESP_LOGD(TAG, "Saving scene %d to flash (not yet implemented)", scene_index);
  // TODO: Implement JSON saving to /assets/scenes/scene_XXX.json
  return ESP_OK;
}

esp_err_t scene_load_manifest(void) {
  ESP_LOGD(TAG, "Loading scene manifest from flash (not yet implemented)");
  // TODO: Implement manifest loading from /assets/scenes/manifest.json
  return ESP_ERR_NOT_FOUND;
}

esp_err_t scene_save_manifest(void) {
  ESP_LOGD(TAG, "Saving scene manifest to flash (not yet implemented)");
  // TODO: Implement manifest saving to /assets/scenes/manifest.json
  return ESP_OK;
}

esp_err_t scene_create_new(const char* name) {
  ESP_LOGI(TAG, "Creating new scene: %s (not yet implemented)", name);
  // TODO: Add scene to manifest, create JSON file
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t scene_delete(uint8_t scene_index) {
  ESP_LOGI(TAG, "Deleting scene %d (not yet implemented)", scene_index);
  // TODO: Remove from manifest, delete JSON file
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t scene_duplicate(uint8_t source_index, const char* new_name) {
  ESP_LOGI(TAG, "Duplicating scene %d as '%s' (not yet implemented)", source_index, new_name);
  // TODO: Load source, create new JSON with new name
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index) {
  ESP_LOGI(TAG, "Reordering scene from %d to %d (not yet implemented)", from_index, to_index);
  // TODO: Reorder manifest, save
  return ESP_ERR_NOT_SUPPORTED;
}
