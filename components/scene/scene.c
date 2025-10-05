#include "scene.h"
#include "esp_log.h"
#include "midi_out.h"
#include "event_bus.h"
#include <string.h>

static const char* TAG = "scene";

// Global scene manager instance
static scene_manager_t g_scene_manager = {0};

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
  
  // Default MIDI channel
  scene->midi_channel = 1;
  
  // Default touchwheel mode
  scene->touchwheel_mode = TOUCHWHEEL_MODE_BUTTONS;
  scene->touchwheel_cc.cc_number = 16;  // General Purpose Controller 1
  scene->touchwheel_cc.value = 0;
  scene->touchwheel_cc.channel = 0;  // Inherit from scene
  
  // Initialize touchpad mappings with defaults
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    scene->touchpads[i].cc.cc_number = DEFAULT_CC_NUMBERS[i];
    scene->touchpads[i].cc.value = 127;  // Full on
    scene->touchpads[i].cc.channel = 0;  // Inherit from scene
  }
}

esp_err_t scene_init(void) {
  if (g_scene_manager.initialized) {
    ESP_LOGW(TAG, "Scene manager already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing scene manager");
  
  // Initialize first scene as default
  scene_init_defaults(&g_scene_manager.scenes[0], 0);
  g_scene_manager.current_scene_index = 0;
  g_scene_manager.num_scenes = 1;
  g_scene_manager.initialized = true;
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = 0}
  };
  event_bus_post(&event);
  
  ESP_LOGI(TAG, "Scene manager initialized with %d scene(s)", g_scene_manager.num_scenes);
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
  
  // Initialize scene if needed
  if (scene_index >= g_scene_manager.num_scenes) {
    for (uint8_t i = g_scene_manager.num_scenes; i <= scene_index; i++) {
      scene_init_defaults(&g_scene_manager.scenes[i], i);
    }
    g_scene_manager.num_scenes = scene_index + 1;
  }
  
  if (g_scene_manager.current_scene_index != scene_index) {
    g_scene_manager.current_scene_index = scene_index;
    
    ESP_LOGI(TAG, "Switched to scene %d: %s", scene_index + 1, g_scene_manager.scenes[scene_index].name);
    
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
  uint8_t next_index = g_scene_manager.current_scene_index + 1;
  if (next_index >= MAX_SCENES) next_index = 0;  // Wrap around
  return scene_set_current(next_index);
}

esp_err_t scene_previous(void) {
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

esp_err_t scene_set_midi_channel(uint8_t scene_index, uint8_t channel) {
  if (scene_index >= MAX_SCENES || channel < 1 || channel > 16) return ESP_ERR_INVALID_ARG;
  
  // Ensure scene is initialized
  if (scene_index >= g_scene_manager.num_scenes) scene_set_current(scene_index);
  
  g_scene_manager.scenes[scene_index].midi_channel = channel;
  ESP_LOGI(TAG, "Scene %d MIDI channel set to %d", scene_index + 1, channel);
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
  
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d ch %d", 
    scene_index + 1, pad_index, cc_number, value, 
    channel ? channel : g_scene_manager.scenes[scene_index].midi_channel);
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

uint8_t scene_get_effective_channel(const touchpad_mapping_t* mapping, const scene_t* scene) {
  if (!mapping || !scene) return 1;  // Default MIDI channel
  
  // If mapping has explicit channel, use it; otherwise inherit from scene
  return (mapping->cc.channel > 0) ? mapping->cc.channel : scene->midi_channel;
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
