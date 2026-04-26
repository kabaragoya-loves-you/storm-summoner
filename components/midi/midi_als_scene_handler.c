#include "midi_als_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "expression.h"
#include "lfo.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "als_scene";

static smart_filter_t s_als_filter;

static uint32_t s_last_tempo_apply_ms = 0;
static uint8_t  s_last_applied_midi = 64;

static void apply_tempo_nudge(uint8_t midi_value, scene_t* scene) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_last_tempo_apply_ms < 50) return;
  s_last_tempo_apply_ms = now_ms;
  if (s_last_applied_midi == midi_value) return;
  s_last_applied_midi = midi_value;

  uint8_t pct = scene_get_als_tempo_nudge_pct(scene_get_current_index());
  if (pct > 100) pct = 100;

  int32_t bpm = scene->bpm;
  float scale = ((float)midi_value - 64.0f) / 63.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;
  float factor = 1.0f + scale * ((float)pct / 100.0f);
  int32_t new_bpm = (int32_t)((float)bpm * factor + 0.5f);
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;

  tempo_set_bpm((uint16_t)new_bpm);
  ESP_LOGD(TAG, "ALS tempo nudge: midi=%u pct=%u -> bpm=%d (base=%d)",
    (unsigned)midi_value, (unsigned)pct, (int)new_bpm, (int)bpm);
}

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
  if (scene_is_input_suspended()) return;
  
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
  
  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
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
      break;
    }
    
    case OUTPUT_TYPE_LFO_RATE: {
      // ALS -> LFO rate modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(1, output_value);
      }
      ESP_LOGD(TAG, "ALS -> LFO rate: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_LFO_DEPTH: {
      // ALS -> LFO depth modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(1, output_value);
      }
      ESP_LOGD(TAG, "ALS -> LFO depth: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_tempo_nudge(output_value, scene);
      break;

    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "ALS: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

void midi_als_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->als;
  if (mapping->note_active) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    send_note_off(channel, mapping->last_note, 0);
    ESP_LOGI(TAG, "ALS Note Off (programming mode): %d", mapping->last_note);
    mapping->note_active = false;
  }
}

esp_err_t midi_als_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing ALS scene handler");
  
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

