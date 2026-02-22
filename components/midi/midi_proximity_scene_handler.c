#include "midi_proximity_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "sensor.h"
#include "expression.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "proximity_scene";

static smart_filter_t s_proximity_filter;
static uint32_t s_note_at_rest_start = 0;  // When sensor went below threshold (ms)
static bool s_note_timing_active = false;  // True when tracking at-rest duration

// Get velocity based on velocity mode setting
static uint8_t get_proximity_velocity(continuous_mapping_t* mapping) {
  velocity_mode_t vel_mode = scene_get_proximity_velocity_mode(scene_get_current_index());
  
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

// Handle proximity sensor events through scene mapping
static void handle_proximity_event(const event_t* event, void* context) {
  if (event->type != EVENT_SENSOR_PROXIMITY) return;
  if (scene_is_input_suspended()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->proximity;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  
  // Handle note mode out-of-range silence
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);  // Convert to ms
    
    if (raw_value < 5) {
      // Sensor is out of range
      if (proximity_get_note_silence_on_low()) {
        // Track when we went below threshold
        if (!s_note_timing_active) {
          s_note_at_rest_start = now;
          s_note_timing_active = true;
        } else {
          uint32_t timeout_ms = proximity_get_timeout_ms();
          // Unsigned subtraction handles wraparound correctly
          if ((now - s_note_at_rest_start) >= timeout_ms) {
            // Timeout expired - silence the note
            if (mapping->note_active) {
              uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
              send_note_off(channel, mapping->last_note, 0);
              ESP_LOGD(TAG, "Proximity Note Off (timeout): %d", mapping->last_note);
              mapping->note_active = false;
            }
          }
        }
      }
      return;  // Below threshold, skip normal processing
    } else {
      s_note_timing_active = false;  // Reset timer when user interacts
    }
  }
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_proximity_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_note) {
      send_note_off(channel, mapping->last_note, 0);
      ESP_LOGD(TAG, "Proximity Note Off: %d", mapping->last_note);
    }
    
    if (!mapping->note_active || note != mapping->last_note) {
      uint8_t velocity = get_proximity_velocity(mapping);
      send_note_on(channel, note, velocity);
      ESP_LOGD(TAG, "Proximity: raw=%d processed=%d -> Note %d vel=%d", raw_value, output_value, note, velocity);
    }
    
    mapping->note_active = true;
    mapping->last_note = note;
  } else {
    uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "Proximity: %d -> CC=%d", raw_value, output_value);
  }
}

void midi_proximity_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->proximity;
  if (mapping->note_active) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    send_note_off(channel, mapping->last_note, 0);
    ESP_LOGI(TAG, "Proximity Note Off (programming mode): %d", mapping->last_note);
    mapping->note_active = false;
  }
}

esp_err_t midi_proximity_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing proximity scene handler");
  
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

