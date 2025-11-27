#include "midi_proximity_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "esp_log.h"

static const char* TAG = "proximity_scene";

static smart_filter_t s_proximity_filter;

// Handle proximity sensor events through scene mapping
static void handle_proximity_event(const event_t* event, void* context) {
  if (event->type != EVENT_SENSOR_PROXIMITY) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->proximity;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_proximity_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_value) {
      send_note_off(channel, mapping->last_value, 0);
      ESP_LOGD(TAG, "Proximity Note Off: %d", mapping->last_value);
    }
    
    send_note_on(channel, note, mapping->velocity);
    mapping->note_active = true;
    mapping->last_value = note;
    
    ESP_LOGD(TAG, "Proximity: raw=%d processed=%d -> Note %d vel=%d", raw_value, output_value, note, mapping->velocity);
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "Proximity: %d -> CC=%d", raw_value, output_value);
  }
}

esp_err_t midi_proximity_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing proximity scene handler");
  
  // Initialize smart filter with deadzone=2
  smart_filter_init(&s_proximity_filter, 2);
  
  // Subscribe to proximity sensor events
  esp_err_t ret = event_bus_subscribe(EVENT_SENSOR_PROXIMITY, handle_proximity_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to proximity events");
    return ret;
  }
  
  ESP_LOGI(TAG, "Proximity scene handler initialized (smart filtering enabled)");
  return ESP_OK;
}

