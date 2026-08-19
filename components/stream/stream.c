#include "stream.h"
#include "scene.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "tilt.h"
#include "transport.h"
#include "event_bus.h"
#include "midi_lfo_scene_handler.h"
#include "expression.h"
#include "input_mode.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "stream";

// Runtime mute for sources that have no engine of their own. Tilt/LFO/
// RTG/S+H delegate to hardware or engine APIs instead.
static bool s_gate_expression = true;
static bool s_gate_cv = true;
static bool s_gate_proximity = true;
static bool s_gate_als = true;
static bool s_gate_note_track = true;

static const char* const s_target_strings[] = {
  [STREAM_TARGET_EXPRESSION] = "expression",
  [STREAM_TARGET_CV] = "cv",
  [STREAM_TARGET_PROXIMITY] = "proximity",
  [STREAM_TARGET_ALS] = "als",
  [STREAM_TARGET_NOTE_TRACK] = "note_track",
  [STREAM_TARGET_TILT_X] = "tilt_x",
  [STREAM_TARGET_TILT_Y] = "tilt_y",
  [STREAM_TARGET_TILT_BOTH] = "tilt_both",
  [STREAM_TARGET_LFO1] = "lfo1",
  [STREAM_TARGET_LFO2] = "lfo2",
  [STREAM_TARGET_LFO_BOTH] = "lfo_both",
  [STREAM_TARGET_SAMPLE_HOLD] = "sample_hold",
  [STREAM_TARGET_RTG] = "rtg",
};

static const char* const s_target_labels[] = {
  [STREAM_TARGET_EXPRESSION] = "Expression",
  [STREAM_TARGET_CV] = "CV",
  [STREAM_TARGET_PROXIMITY] = "Proximity",
  [STREAM_TARGET_ALS] = "ALS",
  [STREAM_TARGET_NOTE_TRACK] = "Note Track",
  [STREAM_TARGET_TILT_X] = "Tilt X",
  [STREAM_TARGET_TILT_Y] = "Tilt Y",
  [STREAM_TARGET_TILT_BOTH] = "Tilt X+Y",
  [STREAM_TARGET_LFO1] = "LFO 1",
  [STREAM_TARGET_LFO2] = "LFO 2",
  [STREAM_TARGET_LFO_BOTH] = "LFO 1+2",
  [STREAM_TARGET_SAMPLE_HOLD] = "S+H",
  [STREAM_TARGET_RTG] = "RTG",
};

stream_target_t stream_target_from_string(const char* str) {
  if (!str || !str[0]) return STREAM_TARGET_DEFAULT;
  for (int i = 0; i < STREAM_TARGET_COUNT; i++) {
    if (strcmp(str, s_target_strings[i]) == 0) return (stream_target_t)i;
  }
  // Legacy Tilt action target strings.
  if (strcmp(str, "x") == 0) return STREAM_TARGET_TILT_X;
  if (strcmp(str, "y") == 0) return STREAM_TARGET_TILT_Y;
  if (strcmp(str, "both") == 0) return STREAM_TARGET_TILT_BOTH;
  return STREAM_TARGET_DEFAULT;
}

const char* stream_target_to_string(stream_target_t target) {
  if (target >= STREAM_TARGET_COUNT) return s_target_strings[STREAM_TARGET_DEFAULT];
  return s_target_strings[target];
}

const char* stream_target_display_name(stream_target_t target) {
  if (target >= STREAM_TARGET_COUNT) return s_target_labels[STREAM_TARGET_DEFAULT];
  return s_target_labels[target];
}

bool stream_target_configured(const scene_t* scene, stream_target_t target) {
  if (!scene) return false;
  switch (target) {
    case STREAM_TARGET_EXPRESSION:
      return scene->expression_mode == EXPRESSION_MODE_PEDAL && scene->expression.enabled;
    case STREAM_TARGET_CV:
      return (scene->cv_input_mode == INPUT_MODE_CV ||
        scene->cv_input_mode == INPUT_MODE_AUDIO) && scene->cv.enabled;
    case STREAM_TARGET_PROXIMITY:
      return scene->proximity.enabled;
    case STREAM_TARGET_ALS:
      return scene->als.enabled;
    case STREAM_TARGET_NOTE_TRACK:
      return scene->note_track.enabled;
    case STREAM_TARGET_TILT_X:
      return scene->tilt_x.enabled;
    case STREAM_TARGET_TILT_Y:
      return scene->tilt_y.enabled;
    case STREAM_TARGET_TILT_BOTH:
      return scene->tilt_x.enabled || scene->tilt_y.enabled;
    case STREAM_TARGET_LFO1:
      return scene->lfo1.enabled;
    case STREAM_TARGET_LFO2:
      return scene->lfo2.enabled;
    case STREAM_TARGET_LFO_BOTH:
      return scene->lfo1.enabled || scene->lfo2.enabled;
    case STREAM_TARGET_SAMPLE_HOLD:
      return scene->sample_hold_config.enabled;
    case STREAM_TARGET_RTG:
      return scene->rtg_config.enabled;
    default:
      return false;
  }
}

static void lfo_stop_slot(uint8_t slot) {
  if (lfo_is_enabled(slot)) {
    if (lfo_get_restore_on_stop(slot))
      midi_lfo_scene_handler_restore_value(slot);
    midi_lfo_scene_handler_release_notes_for_slot(slot);
    lfo_enable(slot, false);
  } else if (lfo_is_pending_start(slot)) {
    lfo_enable(slot, false);
  }
}

static void lfo_start_slot(uint8_t slot) {
  lfo_trigger_start(slot);
}

static void lfo_toggle_slot(uint8_t slot) {
  if (lfo_is_enabled(slot) || lfo_is_pending_start(slot))
    lfo_stop_slot(slot);
  else
    lfo_start_slot(slot);
}

static bool* gate_for_target(stream_target_t target) {
  switch (target) {
    case STREAM_TARGET_EXPRESSION: return &s_gate_expression;
    case STREAM_TARGET_CV: return &s_gate_cv;
    case STREAM_TARGET_PROXIMITY: return &s_gate_proximity;
    case STREAM_TARGET_ALS: return &s_gate_als;
    case STREAM_TARGET_NOTE_TRACK: return &s_gate_note_track;
    default: return NULL;
  }
}

bool stream_is_active(stream_target_t target) {
  switch (target) {
    case STREAM_TARGET_EXPRESSION: return s_gate_expression;
    case STREAM_TARGET_CV: return s_gate_cv;
    case STREAM_TARGET_PROXIMITY: return s_gate_proximity;
    case STREAM_TARGET_ALS: return s_gate_als;
    case STREAM_TARGET_NOTE_TRACK: return s_gate_note_track;
    case STREAM_TARGET_TILT_X: return tilt_axis_get_enabled(TILT_AXIS_X);
    case STREAM_TARGET_TILT_Y: return tilt_axis_get_enabled(TILT_AXIS_Y);
    case STREAM_TARGET_TILT_BOTH:
      return tilt_axis_get_enabled(TILT_AXIS_X) || tilt_axis_get_enabled(TILT_AXIS_Y);
    case STREAM_TARGET_LFO1: return lfo_is_enabled(0) || lfo_is_pending_start(0);
    case STREAM_TARGET_LFO2: return lfo_is_enabled(1) || lfo_is_pending_start(1);
    case STREAM_TARGET_LFO_BOTH:
      return lfo_is_enabled(0) || lfo_is_pending_start(0) ||
        lfo_is_enabled(1) || lfo_is_pending_start(1);
    case STREAM_TARGET_SAMPLE_HOLD: return sample_hold_is_running();
    case STREAM_TARGET_RTG: return rtg_is_running();
    default: return false;
  }
}

uint8_t stream_snapshot_active(stream_target_t target) {
  scene_t* scene = scene_get_current();
  if (!scene) return 0;
  switch (target) {
    case STREAM_TARGET_TILT_BOTH: {
      uint8_t mask = 0;
      if (stream_target_configured(scene, STREAM_TARGET_TILT_X) &&
          stream_is_active(STREAM_TARGET_TILT_X))
        mask |= 1;
      if (stream_target_configured(scene, STREAM_TARGET_TILT_Y) &&
          stream_is_active(STREAM_TARGET_TILT_Y))
        mask |= 2;
      return mask;
    }
    case STREAM_TARGET_LFO_BOTH: {
      uint8_t mask = 0;
      if (stream_target_configured(scene, STREAM_TARGET_LFO1) &&
          stream_is_active(STREAM_TARGET_LFO1))
        mask |= 1;
      if (stream_target_configured(scene, STREAM_TARGET_LFO2) &&
          stream_is_active(STREAM_TARGET_LFO2))
        mask |= 2;
      return mask;
    }
    default:
      return stream_is_active(target) ? 1 : 0;
  }
}

void stream_restore_active(stream_target_t target, uint8_t mask) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  switch (target) {
    case STREAM_TARGET_TILT_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_TILT_X))
        stream_set_active(STREAM_TARGET_TILT_X, (mask & 1) != 0);
      if (stream_target_configured(scene, STREAM_TARGET_TILT_Y))
        stream_set_active(STREAM_TARGET_TILT_Y, (mask & 2) != 0);
      return;
    case STREAM_TARGET_LFO_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_LFO1))
        stream_set_active(STREAM_TARGET_LFO1, (mask & 1) != 0);
      if (stream_target_configured(scene, STREAM_TARGET_LFO2))
        stream_set_active(STREAM_TARGET_LFO2, (mask & 2) != 0);
      return;
    default:
      stream_set_active(target, (mask & 1) != 0);
      return;
  }
}

void stream_set_active(stream_target_t target, bool active) {
  scene_t* scene = scene_get_current();
  if (!scene) return;

  switch (target) {
    case STREAM_TARGET_TILT_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_TILT_X))
        tilt_axis_set_enabled(TILT_AXIS_X, active);
      if (stream_target_configured(scene, STREAM_TARGET_TILT_Y))
        tilt_axis_set_enabled(TILT_AXIS_Y, active);
      return;
    case STREAM_TARGET_LFO_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_LFO1)) {
        if (active) lfo_start_slot(0);
        else lfo_stop_slot(0);
      }
      if (stream_target_configured(scene, STREAM_TARGET_LFO2)) {
        if (active) lfo_start_slot(1);
        else lfo_stop_slot(1);
      }
      return;
    default:
      break;
  }

  if (!stream_target_configured(scene, target)) return;

  bool* gate = gate_for_target(target);
  if (gate) {
    *gate = active;
    return;
  }

  switch (target) {
    case STREAM_TARGET_TILT_X:
      tilt_axis_set_enabled(TILT_AXIS_X, active);
      break;
    case STREAM_TARGET_TILT_Y:
      tilt_axis_set_enabled(TILT_AXIS_Y, active);
      break;
    case STREAM_TARGET_LFO1:
      if (active) lfo_start_slot(0);
      else lfo_stop_slot(0);
      break;
    case STREAM_TARGET_LFO2:
      if (active) lfo_start_slot(1);
      else lfo_stop_slot(1);
      break;
    case STREAM_TARGET_SAMPLE_HOLD:
      if (active) sample_hold_start();
      else sample_hold_stop();
      break;
    case STREAM_TARGET_RTG:
      if (active) rtg_start();
      else rtg_stop();
      break;
    default:
      break;
  }
}

void stream_toggle(stream_target_t target) {
  scene_t* scene = scene_get_current();
  if (!scene) return;

  switch (target) {
    case STREAM_TARGET_TILT_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_TILT_X))
        tilt_axis_set_enabled(TILT_AXIS_X, !tilt_axis_get_enabled(TILT_AXIS_X));
      if (stream_target_configured(scene, STREAM_TARGET_TILT_Y))
        tilt_axis_set_enabled(TILT_AXIS_Y, !tilt_axis_get_enabled(TILT_AXIS_Y));
      return;
    case STREAM_TARGET_LFO_BOTH:
      if (stream_target_configured(scene, STREAM_TARGET_LFO1))
        lfo_toggle_slot(0);
      if (stream_target_configured(scene, STREAM_TARGET_LFO2))
        lfo_toggle_slot(1);
      return;
    default:
      break;
  }

  if (!stream_target_configured(scene, target)) return;

  bool* gate = gate_for_target(target);
  if (gate) {
    *gate = !(*gate);
    return;
  }

  switch (target) {
    case STREAM_TARGET_TILT_X:
      tilt_axis_set_enabled(TILT_AXIS_X, !tilt_axis_get_enabled(TILT_AXIS_X));
      break;
    case STREAM_TARGET_TILT_Y:
      tilt_axis_set_enabled(TILT_AXIS_Y, !tilt_axis_get_enabled(TILT_AXIS_Y));
      break;
    case STREAM_TARGET_LFO1:
      lfo_toggle_slot(0);
      break;
    case STREAM_TARGET_LFO2:
      lfo_toggle_slot(1);
      break;
    case STREAM_TARGET_SAMPLE_HOLD:
      sample_hold_toggle();
      break;
    case STREAM_TARGET_RTG:
      rtg_toggle();
      break;
    default:
      break;
  }
}

static void apply_mapping_start_mode(const continuous_mapping_t* m, bool* gate) {
  if (!m || !m->enabled) {
    *gate = false;
    return;
  }
  switch (m->start_mode) {
    case CONTINUOUS_START_PAUSED:
      *gate = false;
      break;
    case CONTINUOUS_START_TRANSPORT:
      *gate = transport_is_playing();
      break;
    case CONTINUOUS_START_RUNNING:
    default:
      *gate = true;
      break;
  }
}

static void apply_tilt_axis_start_mode(tilt_axis_t axis, const continuous_mapping_t* m) {
  if (!m || !m->enabled) {
    tilt_axis_set_enabled(axis, false);
    return;
  }
  switch (m->start_mode) {
    case CONTINUOUS_START_PAUSED:
      tilt_axis_set_enabled(axis, false);
      break;
    case CONTINUOUS_START_TRANSPORT:
      tilt_axis_set_enabled(axis, transport_is_playing());
      break;
    case CONTINUOUS_START_RUNNING:
    default:
      tilt_axis_set_enabled(axis, true);
      break;
  }
}

void stream_apply_start_modes_for(const scene_t* scene) {
  if (!scene) return;
  apply_mapping_start_mode(&scene->expression, &s_gate_expression);
  apply_mapping_start_mode(&scene->cv, &s_gate_cv);
  apply_mapping_start_mode(&scene->proximity, &s_gate_proximity);
  apply_mapping_start_mode(&scene->als, &s_gate_als);
  apply_mapping_start_mode(&scene->note_track, &s_gate_note_track);
  apply_tilt_axis_start_mode(TILT_AXIS_X, &scene->tilt_x);
  apply_tilt_axis_start_mode(TILT_AXIS_Y, &scene->tilt_y);
}

void stream_apply_start_modes(void) {
  stream_apply_start_modes_for(scene_get_current());
}

static void handle_transport_event(const event_t* event, void* context) {
  if (event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  (void)context;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  bool playing = transport_is_playing();

  if (scene->expression.enabled &&
      scene->expression.start_mode == CONTINUOUS_START_TRANSPORT)
    s_gate_expression = playing;
  if (scene->cv.enabled &&
      scene->cv.start_mode == CONTINUOUS_START_TRANSPORT)
    s_gate_cv = playing;
  if (scene->proximity.enabled &&
      scene->proximity.start_mode == CONTINUOUS_START_TRANSPORT)
    s_gate_proximity = playing;
  if (scene->als.enabled &&
      scene->als.start_mode == CONTINUOUS_START_TRANSPORT)
    s_gate_als = playing;
  if (scene->note_track.enabled &&
      scene->note_track.start_mode == CONTINUOUS_START_TRANSPORT)
    s_gate_note_track = playing;
  if (scene->tilt_x.enabled &&
      scene->tilt_x.start_mode == CONTINUOUS_START_TRANSPORT)
    tilt_axis_set_enabled(TILT_AXIS_X, playing);
  if (scene->tilt_y.enabled &&
      scene->tilt_y.start_mode == CONTINUOUS_START_TRANSPORT)
    tilt_axis_set_enabled(TILT_AXIS_Y, playing);
}

esp_err_t stream_init(void) {
  esp_err_t ret = event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED,
    handle_transport_event, NULL);
  if (ret != ESP_OK) return ret;
  ESP_LOGI(TAG, "Stream runtime initialized");
  return ESP_OK;
}
