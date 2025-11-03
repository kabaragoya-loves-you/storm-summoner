#include "scene.h"
#include "esp_log.h"
#include "midi_messages.h"
#include "device_config.h"
#include "app_settings.h"
#include "event_bus.h"
#include <string.h>

static const char* TAG = "scene";

// NVS keys
#define NVS_KEY_SCENE_MODE       "scene_mode"
#define NVS_KEY_CHANGE_MODE      "change_mode"

// Global scene manager instance
static scene_manager_t g_scene_manager = {
  .mode = SCENE_MODE_SINGLE,
  .change_mode = CHANGE_MODE_IMMEDIATE,
  .current_scene_index = 0,
  .pending_scene_index = 0,
  .has_pending_change = false,
  .num_scenes = 0,
  .initialized = false
};

// Default CC assignments for initial testing
static const uint8_t DEFAULT_CC_NUMBERS[NUM_TOUCHPADS] = {
  1, 2, 3, 4, 5, 6, 7, 8,     // Touchwheel pads (0-7)
  9, 10, 11, 12               // Additional pads (8-11)
};

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
  
  // Initialize touchpad mappings with defaults
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    scene->touchpads[i].cc.cc_number = DEFAULT_CC_NUMBERS[i];
    scene->touchpads[i].cc.value = 127;  // Full on
    scene->touchpads[i].cc.channel = 0;
  }
}

esp_err_t scene_init(void) {
  if (g_scene_manager.initialized) {
    ESP_LOGW(TAG, "Scene manager already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing scene manager");
  
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
  
  // Initialize first scene as default
  scene_init_defaults(&g_scene_manager.scenes[0], 0);
  g_scene_manager.current_scene_index = 0;
  g_scene_manager.num_scenes = 1;
  g_scene_manager.initialized = true;
  
  const char* mode_str = (g_scene_manager.mode == SCENE_MODE_SINGLE) ? "Single" :
                         (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene manager initialized: mode=%s, scenes=%d", mode_str, g_scene_manager.num_scenes);
  
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
  
  if (scene_index >= MAX_SCENES) {
    ESP_LOGE(TAG, "Invalid scene index %d", scene_index);
    return ESP_ERR_INVALID_ARG;
  }
  
  // In pending mode, set pending instead of changing immediately
  if (g_scene_manager.change_mode == CHANGE_MODE_PENDING) {
    g_scene_manager.pending_scene_index = scene_index;
    g_scene_manager.has_pending_change = true;
    ESP_LOGI(TAG, "Pending scene change to %d (confirm or cancel)", scene_index + 1);
    return ESP_OK;
  }
  
  // Initialize scene if needed
  if (scene_index >= g_scene_manager.num_scenes) {
    for (uint8_t i = g_scene_manager.num_scenes; i <= scene_index; i++) {
      scene_init_defaults(&g_scene_manager.scenes[i], i);
    }
    g_scene_manager.num_scenes = scene_index + 1;
  }
  
  if (g_scene_manager.current_scene_index != scene_index) {
    scene_t* new_scene = &g_scene_manager.scenes[scene_index];
    g_scene_manager.current_scene_index = scene_index;
    
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
  }
  
  return ESP_OK;
}

uint8_t scene_get_current_index(void) {
  return g_scene_manager.current_scene_index;
}

scene_t* scene_get_current(void) {
  if (!g_scene_manager.initialized) return NULL;
  return &g_scene_manager.scenes[g_scene_manager.current_scene_index];
}

esp_err_t scene_next(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  uint8_t next_index = g_scene_manager.current_scene_index + 1;
  if (next_index >= MAX_SCENES) next_index = 0;  // Wrap around
  return scene_set_current(next_index);
}

esp_err_t scene_previous(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  uint8_t prev_index;
  if (g_scene_manager.current_scene_index == 0) {
    // Find the highest initialized scene or wrap to MAX_SCENES-1
    prev_index = (g_scene_manager.num_scenes > 1) ? g_scene_manager.num_scenes - 1 : MAX_SCENES - 1;
  } else {
    prev_index = g_scene_manager.current_scene_index - 1;
  }
  return scene_set_current(prev_index);
}

esp_err_t scene_set_name(uint8_t scene_index, const char* name) {
  if (scene_index >= MAX_SCENES || !name) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);  // This will initialize it
  
  strncpy(g_scene_manager.scenes[scene_index].name, name, sizeof(g_scene_manager.scenes[scene_index].name) - 1);
  g_scene_manager.scenes[scene_index].name[sizeof(g_scene_manager.scenes[scene_index].name) - 1] = '\0';
  
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
  if (scene_index >= MAX_SCENES || program > 127) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  g_scene_manager.scenes[scene_index].program_number = program;
  ESP_LOGI(TAG, "Scene %d program number set to %d", scene_index + 1, program);
  return ESP_OK;
}

esp_err_t scene_set_send_pc(uint8_t scene_index, bool send_pc) {
  if (scene_index >= MAX_SCENES) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  g_scene_manager.scenes[scene_index].send_pc_on_change = send_pc;
  ESP_LOGI(TAG, "Scene %d send PC on change: %s", scene_index + 1, send_pc ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index >= MAX_SCENES) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  g_scene_manager.scenes[scene_index].touchwheel_mode = mode;
  ESP_LOGI(TAG, "Scene %d touchwheel mode set to %s", scene_index + 1, mode == TOUCHWHEEL_MODE_BUTTONS ? "buttons" : "encoder");
  return ESP_OK;
}

esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, uint8_t cc_number, uint8_t value, uint8_t channel) {
  if (scene_index >= MAX_SCENES || pad_index >= NUM_TOUCHPADS || cc_number > 127 || value > 127 || channel > 16) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  touchpad_mapping_t* mapping = &g_scene_manager.scenes[scene_index].touchpads[pad_index];
  mapping->cc.cc_number = cc_number;
  mapping->cc.value = value;
  mapping->cc.channel = channel;
  
  uint8_t effective_ch = channel ? channel : device_config_get_channel();
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d ch %d", 
    scene_index + 1, pad_index, cc_number, value, effective_ch);
  return ESP_OK;
}

esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled) {
  if (scene_index >= MAX_SCENES || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  g_scene_manager.scenes[scene_index].touchpads[pad_index].enabled = enabled;
  ESP_LOGI(TAG, "Scene %d pad %d %s", scene_index + 1, pad_index, enabled ? "enabled" : "disabled");
  return ESP_OK;
}

touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index) {
  if (scene_index >= MAX_SCENES || pad_index >= NUM_TOUCHPADS || scene_index >= g_scene_manager.num_scenes) return NULL;
  
  return &g_scene_manager.scenes[scene_index].touchpads[pad_index];
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
  
  // Simple button mode - send CC on press, 0 on release
  uint8_t channel = scene_get_effective_channel(mapping, scene);
  uint8_t value = pressed ? mapping->cc.value : 0;
  
  send_control_change(channel - 1, mapping->cc.cc_number, value);
  esp_err_t ret = ESP_OK;
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send CC message");
    return ret;
  }
  
  ESP_LOGD(TAG, "Pad %d %s: CC%d=%d ch%d", pad_index, pressed ? "pressed" : "released", mapping->cc.cc_number, value, channel);
  
  return ESP_OK;
}
