#include "midi_touchwheel_scene_handler.h"
#include "scene.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "touchwheel_scene";

// Note: Touchwheel events are now handled via callback outputs in scene.c
// This handler is kept for compatibility but typically won't receive events
// from the scene touchwheel instance (which uses callbacks, not event bus)
static void handle_touchwheel_event(const event_t* event, void* context) {
  if (event->type != EVENT_TOUCHWHEEL_VALUE) return;
  
  // Log for debugging if we ever receive an event here
  int value = event->data.touchwheel_value.value;
  ESP_LOGD(TAG, "Received touchwheel event (value=%d) - not processed by scene handler", value);
}

esp_err_t midi_touchwheel_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing touchwheel scene handler");
  
  esp_err_t ret = event_bus_subscribe(EVENT_TOUCHWHEEL_VALUE, handle_touchwheel_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to touchwheel events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Touchwheel scene handler initialized");
  return ESP_OK;
}


