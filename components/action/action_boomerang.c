#include "action_internal.h"
#include "midi_messages.h"
#include "device_config.h"
#include "scene.h"
#include "transport.h"
#include "tempo.h"
#include "tempo_nudge.h"
#include "assets_manager.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "curve.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "action_boomerang";

// ============================================================================
// Boomerang (ADSR envelope) System
// ============================================================================

#define MAX_ACTIVE_BOOMERANGS 4

typedef enum {
  BOOMERANG_PHASE_ATTACK = 0,
  BOOMERANG_PHASE_SUSTAIN,
  BOOMERANG_PHASE_RELEASE,
  BOOMERANG_PHASE_DONE
} boomerang_phase_t;

typedef struct {
  bool active;
  boomerang_phase_t phase;

  uint8_t output_type;
  uint8_t lfo_target;
  uint8_t cc_number;

  int32_t start_value;
  int32_t target_value;
  int32_t last_sent_value;

  uint32_t phase_start_ms;
  uint32_t attack_ms;
  uint32_t sustain_ms;
  uint32_t release_ms;

  uint8_t attack_curve;
  uint8_t attack_curve_slope;
  uint8_t release_curve;
  uint8_t release_curve_slope;
} active_boomerang_t;

static active_boomerang_t s_active_boomerangs[MAX_ACTIVE_BOOMERANGS];
static int16_t s_last_pitch_bend = 0;

// Forward decl
static void boomerang_tick_one(active_boomerang_t* b, uint32_t now_ms);

int16_t action_get_last_pitch_bend(void) {
  return s_last_pitch_bend;
}

void action_set_last_pitch_bend(int16_t value) {
  s_last_pitch_bend = value;
}

void action_boomerang_init(void) {
  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    s_active_boomerangs[i].active = false;
  }
  s_last_pitch_bend = 0;
}

void action_boomerang_clear(void) {
  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    s_active_boomerangs[i].active = false;
  }
  action_morph_update_timer();
  ESP_LOGD(TAG, "Cleared all active boomerangs");
}

// Public API wrapper for header compatibility
void action_clear_boomerangs(void) {
  action_boomerang_clear();
}

bool action_boomerang_any_active(void) {
  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    if (s_active_boomerangs[i].active) return true;
  }
  return false;
}

// Convert a boomerang duration mode + params into a millisecond duration
static uint32_t boomerang_phase_duration_ms(uint8_t mode, uint16_t time_ms, uint8_t division,
    uint16_t bpm_x10, uint8_t current_beat, uint8_t beats_per_bar) {
  (void)current_beat;
  (void)beats_per_bar;
  switch (mode) {
    case BOOMERANG_DUR_INSTANT:
      return 0;
    case BOOMERANG_DUR_TIME_MS:
      return (uint32_t)time_ms;
    case BOOMERANG_DUR_DIVISION:
      return action_morph_get_duration_ms((morph_division_t)division, bpm_x10);
    default:
      return 0;
  }
}

static void boomerang_range_for_output(uint8_t output_type, int32_t* out_min, int32_t* out_max) {
  switch (output_type) {
    case OUTPUT_TYPE_PITCH_BEND:
      *out_min = -8192;
      *out_max = 8191;
      break;
    default:
      *out_min = 0;
      *out_max = 127;
      break;
  }
}

// Capture current value for a target into an int32_t in the same units we
// will interpolate in (CC/LFO/RTG/SH/tempo-nudge: 0-127, pitch-bend: -8192..8191)
static int32_t boomerang_capture_start_value(const action_t* action) {
  uint8_t output_type = action->params.boomerang.output_type;
  uint8_t lfo_target = action->params.boomerang.lfo_target;

  switch (output_type) {
    case OUTPUT_TYPE_CC:
      return action_get_cc_value(action->params.boomerang.cc_number);

    case OUTPUT_TYPE_LFO_RATE:
    case OUTPUT_TYPE_LFO2_RATE:
    case OUTPUT_TYPE_LFO1_RATE: {
      uint8_t slot = (lfo_target == LFO_TARGET_LFO2) ? 1 : 0;
      if (output_type == OUTPUT_TYPE_LFO2_RATE) slot = 1;
      if (output_type == OUTPUT_TYPE_LFO1_RATE) slot = 0;
      return lfo_get_dynamic_rate(slot);
    }

    case OUTPUT_TYPE_LFO_DEPTH:
    case OUTPUT_TYPE_LFO2_DEPTH:
    case OUTPUT_TYPE_LFO1_DEPTH: {
      uint8_t slot = (lfo_target == LFO_TARGET_LFO2) ? 1 : 0;
      if (output_type == OUTPUT_TYPE_LFO2_DEPTH) slot = 1;
      if (output_type == OUTPUT_TYPE_LFO1_DEPTH) slot = 0;
      return lfo_get_dynamic_depth(slot);
    }

    case OUTPUT_TYPE_RTG_RATE:
      return rtg_get_dynamic_rate();

    case OUTPUT_TYPE_SH_RATE:
      return sample_hold_get_dynamic_rate();

    case OUTPUT_TYPE_PITCH_BEND:
      return s_last_pitch_bend;

    case OUTPUT_TYPE_TEMPO_NUDGE:
      return 64;

    default:
      return 64;
  }
}

// Dispatch a boomerang value (in native units) to the correct backend
static void boomerang_apply_value(active_boomerang_t* b, int32_t value) {
  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;

  switch (b->output_type) {
    case OUTPUT_TYPE_CC: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      send_control_change(channel, b->cc_number, v8);
      action_set_cc_value(b->cc_number, v8);
      break;
    }

    case OUTPUT_TYPE_LFO_RATE:
    case OUTPUT_TYPE_LFO2_RATE:
    case OUTPUT_TYPE_LFO1_RATE: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      lfo_target_t target = (lfo_target_t)b->lfo_target;
      if (b->output_type == OUTPUT_TYPE_LFO2_RATE) {
        lfo_set_dynamic_rate(1, v8);
      } else if (b->output_type == OUTPUT_TYPE_LFO1_RATE) {
        lfo_set_dynamic_rate(0, v8);
      } else {
        if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) lfo_set_dynamic_rate(0, v8);
        if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) lfo_set_dynamic_rate(1, v8);
      }
      break;
    }

    case OUTPUT_TYPE_LFO_DEPTH:
    case OUTPUT_TYPE_LFO2_DEPTH:
    case OUTPUT_TYPE_LFO1_DEPTH: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      lfo_target_t target = (lfo_target_t)b->lfo_target;
      if (b->output_type == OUTPUT_TYPE_LFO2_DEPTH) {
        lfo_set_dynamic_depth(1, v8);
      } else if (b->output_type == OUTPUT_TYPE_LFO1_DEPTH) {
        lfo_set_dynamic_depth(0, v8);
      } else {
        if (target == LFO_TARGET_LFO1 || target == LFO_TARGET_BOTH) lfo_set_dynamic_depth(0, v8);
        if (target == LFO_TARGET_LFO2 || target == LFO_TARGET_BOTH) lfo_set_dynamic_depth(1, v8);
      }
      break;
    }

    case OUTPUT_TYPE_RTG_RATE: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      rtg_set_dynamic_rate(v8);
      break;
    }

    case OUTPUT_TYPE_SH_RATE: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      sample_hold_set_dynamic_rate(v8);
      break;
    }

    case OUTPUT_TYPE_PITCH_BEND: {
      int16_t pb = (int16_t)((value < -8192) ? -8192 : (value > 8191 ? 8191 : value));
      uint8_t pb_channel = scene_get_note_channel(scene_get_current_index()) - 1;
      send_pitch_bend(pb_channel, pb);
      s_last_pitch_bend = pb;
      break;
    }

    case OUTPUT_TYPE_TEMPO_NUDGE: {
      uint8_t v8 = (value < 0) ? 0 : (value > 127 ? 127 : (uint8_t)value);
      scene_t* scene = scene_get_current();
      if (scene) {
        uint8_t pct = 20;
        int32_t base_bpm_x10 = scene->bpm_x10;
        float scale = ((float)v8 - 64.0f) / 63.0f;
        if (scale > 1.0f) scale = 1.0f;
        if (scale < -1.0f) scale = -1.0f;
        uint16_t new_bpm_x10 = tempo_nudge_compute_bpm_x10((int32_t)base_bpm_x10, pct, scale);
        tempo_set_bpm_x10(new_bpm_x10);
      }
      break;
    }

    default:
      break;
  }

  b->last_sent_value = value;
}

// Interpolate start->target using a curve applied to normalized t in [0, 127]
static int32_t boomerang_interp(int32_t start, int32_t target, uint32_t elapsed_ms,
    uint32_t duration_ms, uint8_t curve_type, uint8_t curve_slope) {
  if (duration_ms == 0) return target;
  if (elapsed_ms >= duration_ms) return target;

  uint32_t t = (elapsed_ms * 127U) / duration_ms;
  if (t > 127) t = 127;

  curve_t c = {
    .type = (curve_type_t)curve_type,
    .slope = (curve_slope_t)curve_slope,
    .custom_data = NULL
  };
  uint8_t shaped = curve_apply(&c, (uint8_t)t);

  int32_t range = target - start;
  int32_t out = start + (range * (int32_t)shaped) / 127;
  return out;
}

// Find a free boomerang slot, or reuse one matching the same output target
static active_boomerang_t* boomerang_find_slot(const action_t* action) {
  uint8_t output_type = action->params.boomerang.output_type;
  uint8_t cc_number = action->params.boomerang.cc_number;
  uint8_t lfo_target = action->params.boomerang.lfo_target;

  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    active_boomerang_t* b = &s_active_boomerangs[i];
    if (!b->active) continue;
    if (b->output_type != output_type) continue;
    if (output_type == OUTPUT_TYPE_CC && b->cc_number != cc_number) continue;
    if ((output_type == OUTPUT_TYPE_LFO_RATE || output_type == OUTPUT_TYPE_LFO_DEPTH) &&
        b->lfo_target != lfo_target) continue;
    return b;
  }
  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    if (!s_active_boomerangs[i].active) return &s_active_boomerangs[i];
  }
  return NULL;
}

static int32_t boomerang_resolve_target(const action_t* action) {
  uint8_t output_type = action->params.boomerang.output_type;
  uint8_t target_mode = action->params.boomerang.target_mode;

  int32_t configured;
  if (output_type == OUTPUT_TYPE_PITCH_BEND) {
    configured = (int32_t)action->params.boomerang.target_value - 8192;
  } else {
    configured = (int32_t)action->params.boomerang.target_value;
  }

  int32_t lo, hi;
  boomerang_range_for_output(output_type, &lo, &hi);

  if (target_mode == BOOMERANG_TARGET_RANDOM) {
    if (output_type == OUTPUT_TYPE_CC) {
      uint8_t scene_index = scene_get_current_index();
      const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
      const midi_control_t* ctrl = device ?
        assets_get_control_by_cc(device, action->params.boomerang.cc_number) : NULL;
      if (ctrl) {
        if (ctrl->discrete_count > 0 && ctrl->discrete_values) {
          uint8_t idx = (uint8_t)(esp_random() % ctrl->discrete_count);
          return (int32_t)ctrl->discrete_values[idx].value;
        }
        uint16_t cmin = ctrl->min;
        uint16_t cmax = ctrl->max > ctrl->min ? ctrl->max : (uint16_t)(ctrl->min + 1);
        return (int32_t)(cmin + (esp_random() % (cmax - cmin + 1)));
      }
    }
    int32_t span = hi - lo + 1;
    if (span <= 1) return lo;
    return lo + (int32_t)(esp_random() % (uint32_t)span);
  }

  if (configured < lo) configured = lo;
  if (configured > hi) configured = hi;
  return configured;
}

bool action_boomerang_start_internal(const action_t* action) {
  if (!action) return false;

  active_boomerang_t* b = boomerang_find_slot(action);
  if (!b) {
    ESP_LOGW(TAG, "No boomerang slot available");
    return false;
  }

  uint16_t bpm_x10 = tempo_get_bpm_x10();
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;
  time_signature_t sig = tempo_get_time_signature();
  uint8_t beats_per_bar = sig.numerator;
  if (beats_per_bar == 0) beats_per_bar = 4;
  uint8_t current_beat = transport_get_current_beat();
  if (current_beat == 0) current_beat = 1;

  b->active = true;
  b->phase = BOOMERANG_PHASE_ATTACK;
  b->output_type = action->params.boomerang.output_type;
  b->lfo_target = action->params.boomerang.lfo_target;
  b->cc_number = action->params.boomerang.cc_number;

  if (action->params.boomerang.start_mode == BOOMERANG_START_EXPLICIT) {
    if (action->params.boomerang.output_type == OUTPUT_TYPE_PITCH_BEND) {
      b->start_value = (int32_t)action->params.boomerang.start_value - 8192;
    } else {
      b->start_value = (int32_t)action->params.boomerang.start_value;
    }
  } else {
    b->start_value = boomerang_capture_start_value(action);
  }
  b->target_value = boomerang_resolve_target(action);
  b->last_sent_value = b->start_value;

  b->attack_ms = boomerang_phase_duration_ms(
    action->params.boomerang.attack_mode,
    action->params.boomerang.attack_time_ms,
    action->params.boomerang.attack_division,
    bpm_x10, current_beat, beats_per_bar);
  b->sustain_ms = boomerang_phase_duration_ms(
    action->params.boomerang.sustain_mode,
    action->params.boomerang.sustain_time_ms,
    action->params.boomerang.sustain_division,
    bpm_x10, current_beat, beats_per_bar);
  b->release_ms = boomerang_phase_duration_ms(
    action->params.boomerang.release_mode,
    action->params.boomerang.release_time_ms,
    action->params.boomerang.release_division,
    bpm_x10, current_beat, beats_per_bar);

  b->attack_curve = action->params.boomerang.attack_curve;
  b->attack_curve_slope = action->params.boomerang.attack_curve_slope;
  b->release_curve = action->params.boomerang.release_curve;
  b->release_curve_slope = action->params.boomerang.release_curve_slope;

  b->phase_start_ms = (uint32_t)(esp_timer_get_time() / 1000);

  ESP_LOGI(TAG, "Boomerang start: output=%u cc=%u target=%d start=%d "
    "A=%ums S=%ums R=%ums",
    (unsigned)b->output_type, (unsigned)b->cc_number,
    (int)b->target_value, (int)b->start_value,
    (unsigned)b->attack_ms, (unsigned)b->sustain_ms, (unsigned)b->release_ms);

  if (b->attack_ms == 0) {
    boomerang_apply_value(b, b->target_value);
    b->phase = BOOMERANG_PHASE_SUSTAIN;
    b->phase_start_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (b->sustain_ms == 0 && b->release_ms == 0) {
      boomerang_apply_value(b, b->start_value);
      b->active = false;
    }
  }

  action_morph_update_timer();
  return true;
}

static void boomerang_tick_one(active_boomerang_t* b, uint32_t now_ms) {
  if (!b->active) return;

  uint32_t elapsed = now_ms - b->phase_start_ms;

  switch (b->phase) {
    case BOOMERANG_PHASE_ATTACK: {
      if (b->attack_ms == 0 || elapsed >= b->attack_ms) {
        boomerang_apply_value(b, b->target_value);
        b->phase = BOOMERANG_PHASE_SUSTAIN;
        b->phase_start_ms = now_ms;
        if (b->sustain_ms == 0 && b->release_ms == 0) {
          boomerang_apply_value(b, b->start_value);
          b->active = false;
        } else if (b->sustain_ms == 0) {
          b->phase = BOOMERANG_PHASE_RELEASE;
        }
      } else {
        int32_t v = boomerang_interp(b->start_value, b->target_value, elapsed,
          b->attack_ms, b->attack_curve, b->attack_curve_slope);
        if (v != b->last_sent_value) boomerang_apply_value(b, v);
      }
      break;
    }

    case BOOMERANG_PHASE_SUSTAIN: {
      if (b->sustain_ms == 0 || elapsed >= b->sustain_ms) {
        b->phase = BOOMERANG_PHASE_RELEASE;
        b->phase_start_ms = now_ms;
        if (b->release_ms == 0) {
          boomerang_apply_value(b, b->start_value);
          b->active = false;
        }
      }
      break;
    }

    case BOOMERANG_PHASE_RELEASE: {
      if (b->release_ms == 0 || elapsed >= b->release_ms) {
        boomerang_apply_value(b, b->start_value);
        b->active = false;
      } else {
        int32_t v = boomerang_interp(b->target_value, b->start_value, elapsed,
          b->release_ms, b->release_curve, b->release_curve_slope);
        if (v != b->last_sent_value) boomerang_apply_value(b, v);
      }
      break;
    }

    case BOOMERANG_PHASE_DONE:
    default:
      b->active = false;
      break;
  }
}

void action_boomerang_tick_all(uint32_t now_ms) {
  for (int i = 0; i < MAX_ACTIVE_BOOMERANGS; i++) {
    active_boomerang_t* b = &s_active_boomerangs[i];
    if (!b->active) continue;
    boomerang_tick_one(b, now_ms);
  }
}
