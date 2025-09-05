#include "event_bus.h"
#include "esp_log.h"
#include "tempo.h"

#define TAG "TEMPO_EVENT"

static void tempo_handle_event(const event_t* event, void* context) {
  if (event->type == EVENT_BUMP_DETECTED) {
    ESP_LOGI(TAG, "Bump event received (intensity: %d)", event->data.bump.intensity);
    
    // Call the tap tempo function
    tempo_tap_event();
  }
}

void tempo_event_handler_init(void) {
  event_bus_subscribe(EVENT_BUMP_DETECTED, tempo_handle_event, NULL);
  ESP_LOGI(TAG, "Tempo event handler initialized");
}
