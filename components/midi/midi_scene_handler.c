#include "midi_scene_handler.h"
#include "scene.h"
#include "device_config.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "midi_scene";

// Handle touch events and convert to MIDI based on scene mappings
static void handle_touch_event(const event_t* event, void* context) {
  if (!event) return;
  
  uint8_t pad_id = event->data.touch.pad_id;
  bool is_pressed = (event->type == EVENT_TOUCH_PRESS);
  
  // Pad 12 is reserved for UI navigation - don't process it in scene mappings
  if (pad_id == 12) return;
  
  ESP_LOGD(TAG, "Touch event: pad %d %s", pad_id, is_pressed ? "pressed" : "released");
  
  // Process through scene mapping
  esp_err_t ret = scene_process_touchpad(pad_id, is_pressed);
  if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to process touchpad %d: %s", pad_id, esp_err_to_name(ret));
}

// Handle button events for scene navigation and program changes
static void handle_button_event(const event_t* event, void* context) {
  if (!event) return;
  
  scene_mode_t scene_mode = scene_get_mode();
  scene_change_mode_t scene_change_mode = scene_get_change_mode();
  pc_change_mode_t pc_mode = device_config_get_pc_mode();
  
  switch (event->type) {
    case EVENT_BUTTON_R_PRESS:
      if (scene_mode == SCENE_MODE_SINGLE) {
        // In single mode, buttons send PC messages to change presets
        device_config_program_next();
        if (pc_mode == PC_MODE_PENDING) {
          ESP_LOGI(TAG, "Pending program: %d", device_config_get_pending_program());
        }
      } else {
        // In other modes, buttons navigate scenes
        esp_err_t ret = scene_next();
        if (ret == ESP_OK && scene_change_mode == CHANGE_MODE_PENDING) {
          ESP_LOGI(TAG, "Pending next scene (index %d)", scene_get_pending_index());
        }
      }
      break;
      
    case EVENT_BUTTON_L_PRESS:
      if (scene_mode == SCENE_MODE_SINGLE) {
        // In single mode, buttons send PC messages to change presets
        device_config_program_prev();
        if (pc_mode == PC_MODE_PENDING) {
          ESP_LOGI(TAG, "Pending program: %d", device_config_get_pending_program());
        }
      } else {
        // In other modes, buttons navigate scenes
        esp_err_t ret = scene_previous();
        if (ret == ESP_OK && scene_change_mode == CHANGE_MODE_PENDING) {
          ESP_LOGI(TAG, "Pending previous scene (index %d)", scene_get_pending_index());
        }
      }
      break;
      
    case EVENT_BUTTON_BOTH_PRESS:
      // Both buttons = confirm pending change
      if (scene_mode == SCENE_MODE_SINGLE) {
        // Confirm pending program change
        if (device_config_has_pending_program()) {
          device_config_confirm_program();
          ESP_LOGI(TAG, "Confirmed program change");
        }
      } else {
        // Confirm pending scene change
        if (scene_change_mode == CHANGE_MODE_PENDING && scene_has_pending_change()) {
          scene_confirm_change();
          ESP_LOGI(TAG, "Confirmed scene change");
        }
      }
      break;
      
    case EVENT_BUTTON_L_LONG_PRESS:
    case EVENT_BUTTON_R_LONG_PRESS:
    case EVENT_BUTTON_BOTH_LONG_PRESS:
      // Long presses reserved for future features
      ESP_LOGD(TAG, "Long press - reserved for future use");
      break;
      
    default:
      break;
  }
}

esp_err_t midi_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing MIDI scene handler");
  
  // Initialize scene manager first
  esp_err_t ret = scene_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize scene manager: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Subscribe to touch events
  ret = event_bus_subscribe(EVENT_TOUCH_PRESS, handle_touch_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to touch press events: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_TOUCH_RELEASE, handle_touch_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to touch release events: %s", esp_err_to_name(ret));
    event_bus_unsubscribe(EVENT_TOUCH_PRESS, handle_touch_event);
    return ret;
  }
  
  // Subscribe to button events for scene navigation
  event_bus_subscribe(EVENT_BUTTON_L_PRESS, handle_button_event, NULL);
  event_bus_subscribe(EVENT_BUTTON_R_PRESS, handle_button_event, NULL);
  event_bus_subscribe(EVENT_BUTTON_BOTH_PRESS, handle_button_event, NULL);
  event_bus_subscribe(EVENT_BUTTON_L_LONG_PRESS, handle_button_event, NULL);
  event_bus_subscribe(EVENT_BUTTON_R_LONG_PRESS, handle_button_event, NULL);
  event_bus_subscribe(EVENT_BUTTON_BOTH_LONG_PRESS, handle_button_event, NULL);
  
  ESP_LOGI(TAG, "MIDI scene handler initialized");
  return ESP_OK;
}


