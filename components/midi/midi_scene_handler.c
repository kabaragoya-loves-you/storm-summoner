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
  
  ESP_LOGD(TAG, "Touch event: pad %d %s", pad_id, is_pressed ? "pressed" : "released");
  
  // Process through scene mapping
  esp_err_t ret = scene_process_touchpad(pad_id, is_pressed);
  if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to process touchpad %d: %s", pad_id, esp_err_to_name(ret));
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
  
  ESP_LOGI(TAG, "MIDI scene handler initialized");
  return ESP_OK;
}


