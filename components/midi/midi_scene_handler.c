#include "midi_scene_handler.h"
#include "scene.h"
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

// Handle button events for scene navigation
static void handle_button_event(const event_t* event, void* context) {
  if (!event) return;
  
  scene_mode_t mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  
  switch (event->type) {
    case EVENT_BUTTON_R_PRESS:
      // Right button = next scene/preset
      if (mode == SCENE_MODE_SINGLE) {
        // In single mode, buttons could send PC messages (future feature)
        ESP_LOGD(TAG, "Right button in single mode - no action");
      } else {
        esp_err_t ret = scene_next();
        if (ret == ESP_OK) {
          if (change_mode == CHANGE_MODE_PENDING) {
            ESP_LOGI(TAG, "Pending next scene (index %d)", scene_get_pending_index());
          }
        }
      }
      break;
      
    case EVENT_BUTTON_L_PRESS:
      // Left button = previous scene/preset
      if (mode == SCENE_MODE_SINGLE) {
        ESP_LOGD(TAG, "Left button in single mode - no action");
      } else {
        esp_err_t ret = scene_previous();
        if (ret == ESP_OK) {
          if (change_mode == CHANGE_MODE_PENDING) {
            ESP_LOGI(TAG, "Pending previous scene (index %d)", scene_get_pending_index());
          }
        }
      }
      break;
      
    case EVENT_BUTTON_BOTH_PRESS:
      // Both buttons = confirm pending change or cancel
      if (change_mode == CHANGE_MODE_PENDING) {
        if (scene_has_pending_change()) {
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


