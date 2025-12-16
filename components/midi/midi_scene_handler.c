#include "midi_scene_handler.h"
#include "scene.h"
#include "device_config.h"
#include "action.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"

static const char* TAG = "midi_scene";

// Handle touch events and convert to MIDI based on scene mappings
static void handle_touch_event(const event_t* event, void* context) {
  if (!event) return;
  
  // Don't process scene actions in programming mode
  if (ui_get_app_mode() == APP_MODE_PROGRAMMING) return;
  
  uint8_t pad_id = event->data.touch.pad_id;
  bool is_pressed = (event->type == EVENT_TOUCH_PRESS);
  
  // Pad 12 is reserved for UI navigation - don't process it in scene mappings
  if (pad_id == 12) return;
  
  ESP_LOGD(TAG, "Touch event: pad %d %s", pad_id, is_pressed ? "pressed" : "released");
  
  // Process through scene mapping
  esp_err_t ret = scene_process_touchpad(pad_id, is_pressed);
  if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to process touchpad %d: %s", pad_id, esp_err_to_name(ret));
}

// Handle button events using scene-based action assignments
static void handle_button_event(const event_t* event, void* context) {
  if (!event) return;
  
  // Don't process scene actions in programming mode
  if (ui_get_app_mode() == APP_MODE_PROGRAMMING) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  action_chain_t* chain = NULL;
  
  switch (event->type) {
    case EVENT_BUTTON_L_PRESS:
      chain = &scene->button_left;
      ESP_LOGI(TAG, "Left button pressed - %d actions", chain->num_actions);
      break;
      
    case EVENT_BUTTON_R_PRESS:
      chain = &scene->button_right;
      ESP_LOGI(TAG, "Right button pressed - %d actions", chain->num_actions);
      break;
      
    case EVENT_BUTTON_BOTH_PRESS:
      chain = &scene->button_both;
      ESP_LOGI(TAG, "Both buttons pressed - %d actions", chain->num_actions);
      break;
      
    case EVENT_BUTTON_L_LONG_PRESS:
    case EVENT_BUTTON_R_LONG_PRESS:
    case EVENT_BUTTON_BOTH_LONG_PRESS:
      // Long presses reserved for future features
      ESP_LOGD(TAG, "Long press - reserved for future use");
      return;
      
    default:
      return;
  }
  
  if (chain && chain->num_actions > 0) {
    action_execute_chain(chain, 127, true);
  }
}

// Handle bump events using scene-based action assignments
static void handle_bump_event(const event_t* event, void* context) {
  if (!event) return;
  
  // Don't process scene actions in programming mode
  if (ui_get_app_mode() == APP_MODE_PROGRAMMING) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  action_chain_t* chain = &scene->bump;
  
  if (chain->num_actions > 0) {
    ESP_LOGI(TAG, "Bump detected - executing %d actions", chain->num_actions);
    action_execute_chain(chain, 127, true);
  } else {
    ESP_LOGD(TAG, "Bump detected - no actions assigned");
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
  
  // Subscribe to bump events
  event_bus_subscribe(EVENT_BUMP_DETECTED, handle_bump_event, NULL);
  
  ESP_LOGI(TAG, "MIDI scene handler initialized");
  return ESP_OK;
}


