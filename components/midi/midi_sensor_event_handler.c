#include "event_bus.h"
#include "esp_log.h"
#include "midi_messages.h"

#define TAG "MIDI_SENSOR_EVENT"

static void midi_sensor_handle_event(const event_t* event, void* context) {
  switch (event->type) {
    case EVENT_SENSOR_ALS:
      ESP_LOGI(TAG, "ALS sensor event: CC%d value=%d", event->data.sensor.controller, event->data.sensor.value);
      send_control_change(event->data.sensor.channel, event->data.sensor.controller, event->data.sensor.value);
      break;
      
    case EVENT_SENSOR_PROXIMITY:
      ESP_LOGI(TAG, "Proximity sensor event: CC%d value=%d", event->data.sensor.controller, event->data.sensor.value);
      send_control_change(event->data.sensor.channel, event->data.sensor.controller, event->data.sensor.value);
      break;
      
    default:
      break;
  }
}

void midi_sensor_event_handler_init(void) {
  event_bus_subscribe(EVENT_SENSOR_ALS, midi_sensor_handle_event, NULL);
  event_bus_subscribe(EVENT_SENSOR_PROXIMITY, midi_sensor_handle_event, NULL);
  ESP_LOGI(TAG, "MIDI sensor event handler initialized");
}
