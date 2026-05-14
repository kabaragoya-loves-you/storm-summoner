#include "midi_cv_scene_handler.h"
#include "scene.h"
#include "midi_local_output.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "device_config.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "lfo.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "cv_scene";
static smart_filter_t s_cv_filter;

static uint32_t s_last_tempo_apply_ms = 0;
static uint8_t  s_last_applied_midi = 64;

static void apply_tempo_nudge(uint8_t midi_value, scene_t* scene) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_last_tempo_apply_ms < 50) return;
  s_last_tempo_apply_ms = now_ms;
  if (s_last_applied_midi == midi_value) return;
  s_last_applied_midi = midi_value;

  uint8_t pct = scene_get_cv_tempo_nudge_pct(scene_get_current_index());
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
  ESP_LOGD(TAG, "CV tempo nudge: midi=%u pct=%u -> bpm=%d (base=%d)",
    (unsigned)midi_value, (unsigned)pct, (int)new_bpm, (int)bpm);
}

static void handle_cv_event(const event_t* event, void* context) {
  if (event->type != EVENT_CV_VALUE) return;
  
  // Don't send MIDI when on-device output is silenced (programming mode)
  if (!midi_local_output_is_enabled()) return;
  
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
    
    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_tempo_nudge(output_value, scene);
      break;

    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      ESP_LOGD(TAG, "CV: %d -> CC=%d", raw_value, output_value);
      break;
    }
  }
}

// On scene change, drop all of the across-event state we cache so the new
// scene's first event isn't compared against (or snapped to) values captured
// under the previous scene's curve/polarity/extremes.
static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  smart_filter_reset(&s_cv_filter);
  s_last_tempo_apply_ms = 0;
  s_last_applied_midi = 64;
}

esp_err_t midi_cv_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing CV scene handler");
  
  smart_filter_init(&s_cv_filter, 2);
  
  esp_err_t ret = event_bus_subscribe(EVENT_CV_VALUE, handle_cv_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CV events");
    return ret;
  }

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to scene changed events");
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

