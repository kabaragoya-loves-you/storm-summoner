#include "midi_als_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "expression.h"
#include "esp_log.h"

static const char* TAG = "als_scene";

static smart_filter_t s_als_filter;

// Get velocity based on velocity mode setting
static uint8_t get_als_velocity(continuous_mapping_t* mapping) {
  velocity_mode_t vel_mode = scene_get_als_velocity_mode(scene_get_current_index());
  
  switch (vel_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE:
      // Use current expression value (0.0-1.0) as velocity source
      {
        float expr_value = expression_get_value();
        uint8_t vel = 1 + (uint8_t)(expr_value * 126.0f);
        if (vel > 127) vel = 127;
        return vel;
      }
    case VELOCITY_MODE_FIXED:
    default:
      return mapping->velocity;
  }
}

// Handle ALS sensor events through scene mapping
static void handle_als_event(const event_t* event, void* context) {
  if (event->type != EVENT_SENSOR_ALS) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->als;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_als_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_note) {
      send_note_off(channel, mapping->last_note, 0);
      ESP_LOGD(TAG, "ALS Note Off: %d", mapping->last_note);
    }
    
    if (!mapping->note_active || note != mapping->last_note) {
      uint8_t velocity = get_als_velocity(mapping);
      send_note_on(channel, note, velocity);
      ESP_LOGD(TAG, "ALS: %d -> Note %d vel=%d", raw_value, note, velocity);
    }
    
    mapping->note_active = true;
    mapping->last_note = note;
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "ALS: %d -> CC=%d", raw_value, output_value);
  }
}

esp_err_t midi_als_scene_handler_init(void) {
  ESP_LOGI(TAG, "Initializing ALS scene handler");
  
  // Initialize smart filter with deadzone=2
  smart_filter_init(&s_als_filter, 2);
  
  // Subscribe to ALS sensor events
  esp_err_t ret = event_bus_subscribe(EVENT_SENSOR_ALS, handle_als_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to ALS events");
    return ret;
  }
  
  ESP_LOGI(TAG, "ALS scene handler initialized (smart filtering enabled)");
  return ESP_OK;
}

