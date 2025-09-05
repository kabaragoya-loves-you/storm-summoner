#include "event_bus.h"
#include "esp_log.h"
#include "midi_tempo.h"

#define TAG "MIDI_TEMPO_EVENT"

static void midi_tempo_handle_event(const event_t* event, void* context) {
  if (event->type == EVENT_BUMP_DETECTED) {
    ESP_LOGI(TAG, "Bump event received (intensity: %d)", event->data.bump.intensity);
    
    // Call the tap tempo function
    midi_tempo_tap_event();
  }
}

void midi_tempo_event_handler_init(void) {
  event_bus_subscribe(EVENT_BUMP_DETECTED, midi_tempo_handle_event, NULL);
  ESP_LOGI(TAG, "MIDI tempo event handler initialized");
}
