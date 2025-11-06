#include "event_bus.h"
#include "esp_log.h"
#include "midi_messages.h"

#define TAG "MIDI_SENSOR_EVENT"

// DEPRECATED: This handler sends MIDI directly, bypassing scene routing
// It will be removed once sensor scene handlers are verified working
// Use midi_proximity_scene_handler and midi_als_scene_handler instead

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
  // DEPRECATED: Sensor routing now handled by scene-based handlers
  // Keeping this function for backward compatibility but not subscribing
  ESP_LOGW(TAG, "Legacy MIDI sensor event handler - now using scene-based routing");
  
  // Uncomment these if scene handlers aren't working and you need fallback:
  // event_bus_subscribe(EVENT_SENSOR_ALS, midi_sensor_handle_event, NULL);
  // event_bus_subscribe(EVENT_SENSOR_PROXIMITY, midi_sensor_handle_event, NULL);
}
