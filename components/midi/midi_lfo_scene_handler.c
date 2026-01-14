#include "midi_lfo_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "lfo.h"
#include "expression.h"
#include "esp_log.h"

static const char* TAG = "lfo_scene";

static smart_filter_t s_lfo1_filter;
static smart_filter_t s_lfo2_filter;

// Get velocity based on velocity mode setting for LFO1
static uint8_t get_lfo1_velocity(continuous_mapping_t* mapping) {
  scene_t* scene = scene_get_current();
  if (!scene) return mapping->velocity;
  
  switch (scene->lfo1_velocity_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE: {
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

// Get velocity based on velocity mode setting for LFO2
static uint8_t get_lfo2_velocity(continuous_mapping_t* mapping) {
  scene_t* scene = scene_get_current();
  if (!scene) return mapping->velocity;
  
  switch (scene->lfo2_velocity_mode) {
    case VELOCITY_MODE_TOUCHWHEEL:
      return scene_get_touchwheel_velocity();
    case VELOCITY_MODE_GATE_VOLTAGE: {
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

// Handle LFO1 events through scene mapping
static void handle_lfo1_event(const event_t* event, void* context) {
  if (event->type != EVENT_LFO1_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->lfo1;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  uint8_t channel = device_config_get_channel() - 1;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_lfo1_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_note) {
      send_note_off(channel, mapping->last_note, 0);
      ESP_LOGD(TAG, "LFO1 Note Off: %d", mapping->last_note);
    }
    
    if (!mapping->note_active || note != mapping->last_note) {
      uint8_t velocity = get_lfo1_velocity(mapping);
      send_note_on(channel, note, velocity);
      ESP_LOGD(TAG, "LFO1: raw=%d processed=%d -> Note %d vel=%d", raw_value, output_value, note, velocity);
    }
    
    mapping->note_active = true;
    mapping->last_note = note;
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "LFO1: %d -> CC=%d", raw_value, output_value);
  }
}

// Handle LFO2 events through scene mapping
static void handle_lfo2_event(const event_t* event, void* context) {
  if (event->type != EVENT_LFO2_VALUE) return;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->lfo2;
  if (!mapping->enabled) return;
  
  // Get raw value from event (0-127)
  uint8_t raw_value = event->data.sensor.value;
  uint8_t channel = device_config_get_channel() - 1;
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Apply smart filtering (handles extremes + deadzone)
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_lfo2_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
    
    if (mapping->note_active && note != mapping->last_note) {
      send_note_off(channel, mapping->last_note, 0);
      ESP_LOGD(TAG, "LFO2 Note Off: %d", mapping->last_note);
    }
    
    if (!mapping->note_active || note != mapping->last_note) {
      uint8_t velocity = get_lfo2_velocity(mapping);
      send_note_on(channel, note, velocity);
      ESP_LOGD(TAG, "LFO2: raw=%d processed=%d -> Note %d vel=%d", raw_value, output_value, note, velocity);
    }
    
    mapping->note_active = true;
    mapping->last_note = note;
  } else {
    continuous_mapping_send_cc(mapping, channel, output_value);
    ESP_LOGD(TAG, "LFO2: %d -> CC=%d", raw_value, output_value);
  }
}

void midi_lfo_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  // Release LFO1 notes
  continuous_mapping_t* lfo1 = &scene->lfo1;
  if (lfo1->note_active) {
    send_note_off(channel, lfo1->last_note, 0);
    ESP_LOGI(TAG, "LFO1 Note Off (cleanup): %d", lfo1->last_note);
    lfo1->note_active = false;
  }
  
  // Release LFO2 notes
  continuous_mapping_t* lfo2 = &scene->lfo2;
  if (lfo2->note_active) {
    send_note_off(channel, lfo2->last_note, 0);
    ESP_LOGI(TAG, "LFO2 Note Off (cleanup): %d", lfo2->last_note);
    lfo2->note_active = false;
  }
}

void midi_lfo_scene_handler_restore_value(uint8_t slot) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  // Get the mapping for the specified slot
  continuous_mapping_t* mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  
  // Only restore CC output (notes are released separately)
  if (mapping->output_type != OUTPUT_TYPE_CC) return;
  
  // Get the waveform value at phase 0 (the starting value)
  uint8_t raw_value = lfo_get_value_at_phase(slot, 0);
  
  // Process through curve and polarity
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  // Send CC value
  uint8_t channel = device_config_get_channel() - 1;
  continuous_mapping_send_cc(mapping, channel, processed_value);
  
  ESP_LOGI(TAG, "LFO%d restored to phase-0 value: raw=%d processed=%d", 
    slot + 1, raw_value, processed_value);
}

esp_err_t midi_lfo_scene_handler_init(void) {
  // Initialize smart filters with deadzone=2
  smart_filter_init(&s_lfo1_filter, 2);
  smart_filter_init(&s_lfo2_filter, 2);
  
  // Subscribe to LFO events
  esp_err_t ret = event_bus_subscribe(EVENT_LFO1_VALUE, handle_lfo1_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LFO1 events");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_LFO2_VALUE, handle_lfo2_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to LFO2 events");
    return ret;
  }
  
  ESP_LOGI(TAG, "LFO scene handler initialized");
  return ESP_OK;
}
