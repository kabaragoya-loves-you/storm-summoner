#include "midi_touchwheel_scene_handler.h"
#include "scene.h"
#include "action.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "touchwheel_scene";

static void handle_touchwheel_event(const event_t* event, void* context) {
  if (event->type != EVENT_TOUCHWHEEL_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  // Only process if touchwheel is in encoder mode
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_ENCODER) return;
  
  int value = event->data.touchwheel_value.value;
  
  ESP_LOGD(TAG, "Touchwheel value: %d, executing %d action(s)", 
    value, scene->touchwheel_actions.num_actions);
  
  // Execute touchwheel action chain
  // Use value as parameter (clamped to 0-127 for MIDI compatibility)
  uint8_t action_value = (value < 0) ? 0 : (value > 127) ? 127 : (uint8_t)value;
  action_execute_chain(&scene->touchwheel_actions, action_value, true);
}

esp_err_t midi_touchwheel_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing touchwheel scene handler");
  
  esp_err_t ret = event_bus_subscribe(EVENT_TOUCHWHEEL_VALUE, handle_touchwheel_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to touchwheel events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Touchwheel scene handler initialized");
  return ESP_OK;
}


