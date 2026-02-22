#include "midi_cv_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "lfo.h"
#include "esp_log.h"

static const char* TAG = "cv_scene";
static smart_filter_t s_cv_filter;

static void handle_cv_event(const event_t* event, void* context) {
  if (event->type != EVENT_CV_VALUE) return;
  
  // Don't send MIDI when scene input is suspended (programming mode)
  if (scene_is_input_suspended()) return;
  
  scene_t* scene = scene_get_current();
  if (!scene || !scene->cv.enabled) return;
  
  // Only process CV values in CV or Audio mode
  // CV/Gate mode (INPUT_MODE_NOTE) is handled by input_manager's note handlers
  if (scene->cv_input_mode != INPUT_MODE_CV && scene->cv_input_mode != INPUT_MODE_AUDIO) return;
  
  continuous_mapping_t* mapping = &scene->cv;
  uint8_t raw_value = event->data.cv.midi_value;
  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);
  
  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_cv_filter, processed_value, &value_changed);
  
  if (!value_changed) return;
  
  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(channel, mapping->last_note, 0);
        ESP_LOGD(TAG, "CV Note Off: %d", mapping->last_note);
      }
      
      if (!mapping->note_active || note != mapping->last_note) {
        send_note_on(channel, note, mapping->velocity);
        ESP_LOGD(TAG, "CV: %d -> Note %d vel=%d", raw_value, note, mapping->velocity);
      }
      
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    
    case OUTPUT_TYPE_LFO_RATE: {
      // CV -> LFO rate modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(1, output_value);
      }
      ESP_LOGD(TAG, "CV -> LFO rate: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_LFO_DEPTH: {
      // CV -> LFO depth modulation
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(1, output_value);
      }
      ESP_LOGD(TAG, "CV -> LFO depth: %d (target: %d)", output_value, target);
      break;
    }
    
    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "CV: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

esp_err_t midi_cv_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing CV scene handler");
  
  smart_filter_init(&s_cv_filter, 2);
  
  esp_err_t ret = event_bus_subscribe(EVENT_CV_VALUE, handle_cv_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CV events");
    return ret;
  }
  
  ESP_LOGI(TAG, "CV scene handler initialized");
  return ESP_OK;
}

void midi_cv_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  continuous_mapping_t* mapping = &scene->cv;
  if (mapping->note_active) {
    uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
    send_note_off(channel, mapping->last_note, 0);
    ESP_LOGI(TAG, "CV Note Off (programming mode): %d", mapping->last_note);
    mapping->note_active = false;
  }
}

