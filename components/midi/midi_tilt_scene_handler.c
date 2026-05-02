#include "midi_tilt_scene_handler.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "smart_filter.h"
#include "midi_messages.h"
#include "event_bus.h"
#include "lfo.h"
#include "tempo.h"
#include "tilt.h"
#include "expression.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "tilt_scene";

typedef struct {
  smart_filter_t filter;
  // Rate-limit tempo updates to ~20 Hz for OUTPUT_TYPE_TEMPO_NUDGE.
  uint32_t last_tempo_apply_ms;
  uint8_t last_applied_midi;

  // Forgiveness-zone auto-note-off bookkeeping. prev_in_zone tracks the
  // raw-event side (pre-continuous-mapping) because that is the signal that
  // definitively says "inside the tilt module's forgive zone" regardless of
  // a scene's per-axis middle_value / min / max remapping.
  bool prev_in_zone;
  esp_timer_handle_t note_off_timer;
  uint8_t pending_note;
  uint8_t pending_channel;
} tilt_state_t;

static tilt_state_t s_state[2];

static void cancel_note_off_timer(int axis) {
  if (s_state[axis].note_off_timer) {
    esp_timer_stop(s_state[axis].note_off_timer);
  }
}

static void note_off_timer_cb(void* arg) {
  int axis = (int)(intptr_t)arg;
  if (axis < 0 || axis > 1) return;

  // Resolve mapping fresh; the scene could have changed since the timer was
  // scheduled. If the note is no longer active (e.g. user tilted back out and
  // the main path released it), this is a no-op.
  scene_t* scene = scene_get_current();
  if (!scene) return;
  continuous_mapping_t* mapping = (axis == 0) ? &scene->tilt_x : &scene->tilt_y;
  if (!mapping->note_active) return;

  send_note_off(s_state[axis].pending_channel, s_state[axis].pending_note, 0);
  mapping->note_active = false;
  ESP_LOGD(TAG, "tilt_%c auto note-off (zone dwell)", axis == 0 ? 'x' : 'y');
}

static void schedule_note_off(int axis, continuous_mapping_t* mapping, uint8_t channel) {
  tilt_note_off_mode_t mode = tilt_get_note_off_mode();
  if (mode == TILT_NOTE_OFF_OFF) return;
  if (!mapping->note_active) return;

  time_signature_t ts = tempo_get_time_signature();
  uint32_t dur_ms = tilt_note_off_duration_ms(tempo_get_bpm(), ts.numerator, ts.denominator);
  if (dur_ms == 0) return;

  if (!s_state[axis].note_off_timer) {
    esp_timer_create_args_t args = {
      .callback = note_off_timer_cb,
      .arg = (void*)(intptr_t)axis,
      .dispatch_method = ESP_TIMER_TASK,
      .name = (axis == 0) ? "tilt_x_off" : "tilt_y_off",
    };
    if (esp_timer_create(&args, &s_state[axis].note_off_timer) != ESP_OK) return;
  }

  s_state[axis].pending_note = mapping->last_note;
  s_state[axis].pending_channel = channel;
  esp_timer_stop(s_state[axis].note_off_timer);
  esp_timer_start_once(s_state[axis].note_off_timer, (uint64_t)dur_ms * 1000ULL);
  ESP_LOGD(TAG, "tilt_%c scheduled note-off in %u ms (note=%u)",
    axis == 0 ? 'x' : 'y', (unsigned)dur_ms, (unsigned)mapping->last_note);
}

static inline continuous_mapping_t* tilt_mapping(scene_t* scene, int axis) {
  return axis == 0 ? &scene->tilt_x : &scene->tilt_y;
}

static uint8_t get_tilt_velocity(int axis, continuous_mapping_t* mapping) {
  velocity_mode_t vel_mode;
  if (axis == 0) vel_mode = scene_get_tilt_x_velocity_mode(scene_get_current_index());
  else           vel_mode = scene_get_tilt_y_velocity_mode(scene_get_current_index());

  switch (vel_mode) {
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

// Midi 0..127 around 64 -> bpm around scene->bpm +/- nudge_pct
static void apply_tempo_nudge(int axis, uint8_t midi_value, scene_t* scene) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  if (now_ms - s_state[axis].last_tempo_apply_ms < 50) return;  // ~20 Hz
  s_state[axis].last_tempo_apply_ms = now_ms;
  if (s_state[axis].last_applied_midi == midi_value) return;
  s_state[axis].last_applied_midi = midi_value;

  uint8_t pct = (axis == 0)
    ? scene_get_tilt_x_tempo_nudge_pct(scene_get_current_index())
    : scene_get_tilt_y_tempo_nudge_pct(scene_get_current_index());
  if (pct > 100) pct = 100;

  int32_t bpm = scene->bpm;
  // signed delta: -1.0 at midi=0, 0 at 64, +1.0 at 127
  float scale = ((float)midi_value - 64.0f) / 63.0f;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;
  float factor = 1.0f + scale * ((float)pct / 100.0f);
  int32_t new_bpm = (int32_t)((float)bpm * factor + 0.5f);
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;

  tempo_set_bpm((uint16_t)new_bpm);
  ESP_LOGD(TAG, "tilt_%c tempo nudge: midi=%u pct=%u -> bpm=%d (base=%d)",
    axis == 0 ? 'x' : 'y', (unsigned)midi_value, (unsigned)pct,
    (int)new_bpm, (int)bpm);
}

static void handle_tilt_event(const event_t* event, void* context) {
  (void)context;
  int axis = (event->type == EVENT_SENSOR_TILT_X) ? 0
           : (event->type == EVENT_SENSOR_TILT_Y) ? 1 : -1;
  if (axis < 0) return;
  if (scene_is_input_suspended()) return;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  continuous_mapping_t* mapping = tilt_mapping(scene, axis);
  if (!mapping->enabled) return;

  uint8_t raw_value = event->data.sensor.value;

  // Track forgive-zone transitions BEFORE smart_filter has a chance to
  // swallow this event: raw 64 is the tilt module's signal for "inside the
  // forgive middle zone", and we want to arm/cancel the auto-note-off timer
  // on transitions regardless of whether the filtered output changes. We
  // also arm here (not inside the NOTE case below) so that a smart_filter
  // dedupe on zone entry doesn't leave the timer un-armed.
  bool in_zone_now = (raw_value == 64);
  bool was_in_zone = s_state[axis].prev_in_zone;
  if (was_in_zone && !in_zone_now) {
    cancel_note_off_timer(axis);
  } else if (!was_in_zone && in_zone_now &&
             mapping->output_type == OUTPUT_TYPE_NOTE &&
             mapping->note_active &&
             tilt_get_forgive_middle()) {
    uint8_t note_ch = scene_get_note_channel(scene_get_current_index()) - 1;
    schedule_note_off(axis, mapping, note_ch);
  }
  s_state[axis].prev_in_zone = in_zone_now;

  uint8_t processed_value = continuous_mapping_process(raw_value, mapping);

  bool value_changed = false;
  uint8_t output_value = smart_filter_process(&s_state[axis].filter, processed_value, &value_changed);
  if (!value_changed) return;

  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;

  switch (mapping->output_type) {
    case OUTPUT_TYPE_NOTE: {
      uint8_t note_channel = scene_get_note_channel(scene_get_current_index()) - 1;
      uint8_t note = continuous_mapping_value_to_note(output_value, mapping);
      if (mapping->note_active && note != mapping->last_note) {
        send_note_off(note_channel, mapping->last_note, 0);
      }
      if (!mapping->note_active || note != mapping->last_note) {
        uint8_t velocity = get_tilt_velocity(axis, mapping);
        send_note_on(note_channel, note, velocity);
      }
      mapping->note_active = true;
      mapping->last_note = note;
      break;
    }
    case OUTPUT_TYPE_LFO_RATE: {
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) lfo_set_dynamic_rate(0, output_value);
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) lfo_set_dynamic_rate(1, output_value);
      break;
    }
    case OUTPUT_TYPE_LFO_DEPTH: {
      lfo_target_t target = mapping->lfo_target;
      if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) lfo_set_dynamic_depth(0, output_value);
      if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) lfo_set_dynamic_depth(1, output_value);
      break;
    }
    case OUTPUT_TYPE_PITCH_BEND: {
      // Bipolar MIDI 0..127 -> pitch bend -8192..+8191
      int32_t pb = ((int32_t)output_value - 64) * 128;
      if (pb > 8191) pb = 8191;
      if (pb < -8192) pb = -8192;
      uint8_t note_channel = scene_get_note_channel(scene_get_current_index()) - 1;
      send_pitch_bend(note_channel, (int16_t)pb);
      break;
    }
    case OUTPUT_TYPE_TEMPO_NUDGE:
      apply_tempo_nudge(axis, output_value, scene);
      break;
    case OUTPUT_TYPE_CC:
    default:
      continuous_mapping_send_cc(mapping, channel, output_value);
      break;
  }
}

void midi_tilt_scene_handler_release_notes(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return;
  for (int axis = 0; axis < 2; axis++) {
    continuous_mapping_t* mapping = tilt_mapping(scene, axis);
    if (mapping->note_active) {
      uint8_t ch = scene_get_note_channel(scene_get_current_index()) - 1;
      send_note_off(ch, mapping->last_note, 0);
      mapping->note_active = false;
    }
  }
}

// On scene change, drop the across-event filter and zone-tracking state for
// both axes so the new scene's first event isn't compared against (or snapped
// to) values captured under the previous scene's curve/polarity/extremes.
// Also cancel any pending forgive-zone note-off timers; they were scheduled
// against the previous scene's mapping->last_note and shouldn't fire blindly
// against whatever the new scene happens to have active.
static void handle_scene_changed(const event_t* event, void* context) {
  (void)event;
  (void)context;
  for (int i = 0; i < 2; i++) {
    cancel_note_off_timer(i);
    smart_filter_reset(&s_state[i].filter);
    s_state[i].last_tempo_apply_ms = 0;
    s_state[i].last_applied_midi = 64;
    // Match init: assume in-zone so the next event doesn't look like a fresh
    // exit from the forgive zone.
    s_state[i].prev_in_zone = true;
    s_state[i].pending_note = 0;
    s_state[i].pending_channel = 0;
  }
}

esp_err_t midi_tilt_scene_handler_init(void) {
  for (int i = 0; i < 2; i++) {
    smart_filter_init(&s_state[i].filter, 2);
    s_state[i].last_tempo_apply_ms = 0;
    s_state[i].last_applied_midi = 64;
    // Start in-zone: avoids a spurious "zone entry" on the first event.
    s_state[i].prev_in_zone = true;
    s_state[i].note_off_timer = NULL;
    s_state[i].pending_note = 0;
    s_state[i].pending_channel = 0;
  }
  esp_err_t ret = event_bus_subscribe(EVENT_SENSOR_TILT_X, handle_tilt_event, NULL);
  if (ret != ESP_OK) return ret;
  ret = event_bus_subscribe(EVENT_SENSOR_TILT_Y, handle_tilt_event, NULL);
  if (ret != ESP_OK) return ret;
  ret = event_bus_subscribe(EVENT_SCENE_CHANGED, handle_scene_changed, NULL);
  if (ret != ESP_OK) return ret;
  ESP_LOGI(TAG, "Tilt scene handler initialized");
  return ESP_OK;
}
