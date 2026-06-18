#include "midi_note_track_scene_handler.h"
#include "scene.h"
#include "midi_local_output.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "midi_messages.h"
#include "midi_in.h"
#include "event_bus.h"
#include "note_track_config.h"
#include "lfo.h"
#include "tempo.h"
#include "tempo_nudge.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "midi_note_track";

static smart_filter_t s_note_track_filter;
static uint32_t s_last_tempo_apply_ms = 0;
static uint8_t  s_last_applied_midi = 64;
static bool s_have_last_value = false;
static uint8_t s_last_value = 0;

static void apply_tempo_nudge(uint8_t midi_value, scene_t* scene) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_last_tempo_apply_ms < 50) return;
  s_last_tempo_apply_ms = now_ms;
  if (s_last_applied_midi == midi_value) return;
  s_last_applied_midi = midi_value;

  // Reuse expression's tempo-nudge percent for now; could be split out later.
  uint8_t pct = scene_get_expression_tempo_nudge_pct(scene_get_current_index());
  if (pct > 100) pct = 100;

  float scale = ((float)midi_value - 64.0f) / 63.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;

  uint16_t new_bpm_x10 = tempo_nudge_compute_bpm_x10(scene->bpm_x10, pct, scale);
  tempo_set_bpm_x10(new_bpm_x10);
  ESP_LOGD(TAG, "Note Track tempo nudge: midi=%u pct=%u -> bpm_x10=%u (base=%u)",
    (unsigned)midi_value, (unsigned)pct, (unsigned)new_bpm_x10,
    (unsigned)scene->bpm_x10);
}

// Map a note number in [low, high] to 0..127. Returns false if out of range.
static bool note_to_value(uint8_t note, uint8_t* out) {
  uint8_t lo = note_track_get_low_note();
  uint8_t hi = note_track_get_high_note();
  if (lo > hi) {
    uint8_t tmp = lo; lo = hi; hi = tmp;
  }
  if (note < lo || note > hi) return false;

  if (lo == hi) {
    *out = 0;
    return true;
  }

  uint32_t span = (uint32_t)(hi - lo);
  uint32_t v = ((uint32_t)(note - lo) * 127u + (span / 2u)) / span;
  if (v > 127u) v = 127u;
  *out = (uint8_t)v;
  return true;
}

static void dispatch_value(uint8_t output_value, scene_t* scene,
                           continuous_mapping_t* mapping) {
  switch (mapping->output_type) {
    case OUTPUT_TYPE_LFO_RATE: {
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_rate(1, output_value);
      }
      break;
    }

    case OUTPUT_TYPE_LFO_DEPTH: {
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(0, output_value);
      }
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) {
        lfo_set_dynamic_depth(1, output_value);
      }
      break;
    }

    case OUTPUT_TYPE_PITCH_BEND: {
      int32_t pb = ((int32_t)output_value - 64) * 128;
      if (pb > 8191) pb = 8191;
      if (pb < -8192) pb = -8192;
      uint8_t channel = scene_get_note_channel(scene_get_current_index()) - 1;
      send_pitch_bend(channel, (int16_t)pb);
      break;
    }

    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_tempo_nudge(output_value, scene);
      break;

    case OUTPUT_TYPE_CC:
    default: {
      uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
      continuous_mapping_send_cc(mapping, channel, output_value);
      break;
    }
  }
}

static void handle_midi_in(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_MIDI_IN) return;
  if (!midi_local_output_is_enabled()) return;

  uint8_t mtype = event->data.midi_in.type;
  uint8_t channel0 = event->data.midi_in.channel;
  uint8_t note = event->data.midi_in.data1;
  uint8_t velocity = event->data.midi_in.data2;

  bool is_note_on = (mtype == MIDI_EVENT_NOTE_ON) && velocity > 0;
  bool is_note_off = (mtype == MIDI_EVENT_NOTE_OFF) ||
                     (mtype == MIDI_EVENT_NOTE_ON && velocity == 0);

  if (!is_note_on && !is_note_off) return;

  if (!note_track_message_matches(channel0, note)) return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = &scene->note_track;
  if (!mapping->enabled) return;

  if (is_note_off) {
    if (mapping->use_idle_value) {
      uint8_t idle = mapping->idle_value;
      uint8_t processed = continuous_mapping_process(idle, mapping);
      bool changed = false;
      uint8_t output_value = smart_filter_process(&s_note_track_filter, processed, &changed);
      if (changed) {
        dispatch_value(output_value, scene, mapping);
        s_last_value = output_value;
        s_have_last_value = true;
      }
    }
    // otherwise hold last value: do nothing on note off
    return;
  }

  uint8_t raw_value = 0;
  if (!note_to_value(note, &raw_value)) return;

  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);

  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_note_track_filter, processed_value, &value_changed);
  if (!value_changed && s_have_last_value) return;

  dispatch_value(output_value, scene, mapping);
  s_last_value = output_value;
  s_have_last_value = true;
}

// Drop everything the handler remembers across notes, so the new scene's
// first incoming note is treated as a fresh value rather than being
// compared against (or snapped to) state captured under the previous scene.
static void reset_runtime_state(void) {
  smart_filter_reset(&s_note_track_filter);
  s_have_last_value = false;
  s_last_value = 0;
  s_last_tempo_apply_ms = 0;
  s_last_applied_midi = 64;
}

static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  reset_runtime_state();
}

esp_err_t midi_note_track_scene_handler_init(void) {
  ESP_LOGD(TAG, "Initializing Note Track scene handler");

  smart_filter_init(&s_note_track_filter, 2);

  esp_err_t ret = event_bus_subscribe(EVENT_MIDI_IN, handle_midi_in, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to EVENT_MIDI_IN: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to EVENT_SCENE_CHANGED: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Note Track scene handler initialized");
  return ESP_OK;
}
