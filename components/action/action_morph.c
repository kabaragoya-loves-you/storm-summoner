#include "action_internal.h"
#include "midi_messages.h"
#include "device_config.h"
#include "scene.h"
#include "transport.h"
#include "tempo.h"
#include "assets_manager.h"
#include "cc_state.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "action_morph";

// ============================================================================
// Morph (interpolation) System
// ============================================================================

#define MAX_ACTIVE_MORPHS 4

typedef struct {
  bool active;
  action_t* action;           // For state sync (cycle index)
  uint8_t num_ccs;
  uint8_t cc_numbers[MORPH_MAX_CCS];
  uint8_t start_values[MORPH_MAX_CCS];    // Values at morph start
  uint8_t target_values[MORPH_MAX_CCS];   // Values at morph end
  uint8_t current_step;       // 0 to total_steps-1
  uint8_t total_steps;
  uint32_t step_interval_ms;  // Time between steps
  uint32_t next_step_time;    // When to send next value (ms timestamp)

  // For SYNC mode: target moment tracking
  morph_timing_mode_t timing_mode;
  uint8_t sync_target_beat;   // Target beat (1-16, 0=any/bar start)
  bool sync_waiting_final;    // True when waiting for beat event to send final value

  // Per-CC runtime state. Discrete tables are looked up on demand via
  // assets_get_control_by_cc (O(1)) — no per-slot cache.
  uint8_t last_sent_values[MORPH_MAX_CCS];
  bool delay_final[MORPH_MAX_CCS];
  bool clear_dirty_on_complete;
} active_morph_t;

static active_morph_t s_active_morphs[MAX_ACTIVE_MORPHS];
static esp_timer_handle_t s_morph_timer = NULL;

// ----------------------------------------------------------------------------
// Tempo morph: minimalist linear BPM ramp. One global slot -- there's only
// one transport tempo, so a second start retargets the in-flight ramp.
// Reuses the shared 10ms timer; integer-BPM throttled so we only call
// tempo_set_bpm() when the value actually changes.
// ----------------------------------------------------------------------------
typedef struct {
  bool active;
  uint16_t start_bpm_x10;
  uint16_t target_bpm_x10;
  uint32_t start_ms;
  uint32_t end_ms;
  uint16_t last_sent_bpm_x10;
} tempo_morph_t;

static tempo_morph_t s_tempo_morph;

// Forward declarations
static void morph_timer_callback(void* arg);
static void morph_beat_event_handler(const event_t* event, void* context);
static void morph_advance_step(active_morph_t* m);
static void morph_send_final_values(active_morph_t* m);

// Calculate optimal step count based on value delta and duration
static uint8_t calculate_auto_steps(uint8_t value_delta, uint32_t duration_ms) {
  uint8_t delta_based = (value_delta + 1) / 2;
  uint8_t duration_based = (uint8_t)(duration_ms / 25);
  uint8_t steps = (delta_based < duration_based) ? delta_based : duration_based;
  if (steps < 4) steps = 4;
  if (steps > 64) steps = 64;
  return steps;
}

// Find the index of a value in a discrete_value_t array.
// Returns the index of the closest matching value, or 0 if not found.
static int find_discrete_index(const discrete_value_t* values, uint8_t count,
    uint8_t target) {
  if (!values || count == 0) return 0;

  int best_idx = 0;
  int best_diff = 255;

  for (int i = 0; i < count; i++) {
    int v = (int)(values[i].value & 0x7F);
    int diff = (target > v) ? (target - v) : (v - target);
    if (diff < best_diff) {
      best_diff = diff;
      best_idx = i;
    }
  }

  return best_idx;
}

static uint32_t get_feel_duration_ms(morph_feel_t feel, uint16_t bpm_x10) {
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;
  uint32_t beat_ms = 600000 / bpm_x10;

  switch (feel) {
    case MORPH_FEEL_FAST:   return beat_ms / 4;
    case MORPH_FEEL_MEDIUM: return beat_ms;
    case MORPH_FEEL_SLOW:   return beat_ms * 2;
    default: return beat_ms;
  }
}

// Get morph duration for DURATION mode (fixed musical duration)
uint32_t action_morph_get_duration_ms(morph_division_t div, uint16_t bpm_x10) {
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;
  uint32_t beat_ms = 600000 / bpm_x10;
  uint8_t felt_beats = tempo_get_felt_beats_per_bar();
  if (felt_beats == 0) felt_beats = 4;

  switch (div) {
    case MORPH_DIV_1_BEAT:  return beat_ms;
    case MORPH_DIV_2_BEATS: return beat_ms * 2;
    case MORPH_DIV_3_BEATS: return beat_ms * 3;

    case MORPH_DIV_1_BAR:   return beat_ms * felt_beats;
    case MORPH_DIV_2_BARS:  return beat_ms * felt_beats * 2;
    case MORPH_DIV_3_BARS:  return beat_ms * felt_beats * 3;
    case MORPH_DIV_4_BARS:  return beat_ms * felt_beats * 4;

    default: return beat_ms;
  }
}

static uint32_t get_sync_duration_ms(morph_division_t div, uint16_t bpm_x10,
    uint8_t current_beat, uint8_t beats_per_bar) {
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;
  if (beats_per_bar == 0) beats_per_bar = 4;
  if (current_beat == 0) current_beat = 1;

  uint32_t beat_ms = 600000 / bpm_x10;
  uint8_t target_beat;
  uint8_t beats_remaining;

  switch (div) {
    case MORPH_DIV_BEAT:
      return beat_ms;

    case MORPH_DIV_BAR:
      target_beat = 1;
      if (current_beat >= target_beat) {
        beats_remaining = beats_per_bar - current_beat + target_beat;
      } else {
        beats_remaining = target_beat - current_beat;
      }
      if (beats_remaining == 0) beats_remaining = beats_per_bar;
      return beat_ms * beats_remaining;

    case MORPH_DIV_2_BARS:
      beats_remaining = beats_per_bar - current_beat + 1;
      beats_remaining += beats_per_bar;
      return beat_ms * beats_remaining;

    case MORPH_DIV_4_BARS:
      beats_remaining = beats_per_bar - current_beat + 1;
      beats_remaining += beats_per_bar * 3;
      return beat_ms * beats_remaining;

    case MORPH_DIV_BEAT_2:
    case MORPH_DIV_BEAT_3:
    case MORPH_DIV_BEAT_4:
      target_beat = (div - MORPH_DIV_BEAT_2) + 2;
      if (target_beat > beats_per_bar) target_beat = beats_per_bar;
      if (current_beat >= target_beat) {
        beats_remaining = beats_per_bar - current_beat + target_beat;
      } else {
        beats_remaining = target_beat - current_beat;
      }
      if (beats_remaining == 0) beats_remaining = beats_per_bar;
      return beat_ms * beats_remaining;

    default:
      return beat_ms;
  }
}

static uint8_t get_sync_target_beat(morph_division_t div) {
  switch (div) {
    case MORPH_DIV_BEAT:   return 0;
    case MORPH_DIV_BAR:    return 1;
    case MORPH_DIV_2_BARS: return 1;
    case MORPH_DIV_4_BARS: return 1;
    case MORPH_DIV_BEAT_2: return 2;
    case MORPH_DIV_BEAT_3: return 3;
    case MORPH_DIV_BEAT_4: return 4;
    default: return 0;
  }
}

static uint8_t get_morph_steps(const action_t* action, uint8_t value_delta,
    uint32_t duration_ms) {
  switch (action->morph_steps_mode) {
    case MORPH_STEPS_AUTO:
      return calculate_auto_steps(value_delta, duration_ms);
    case MORPH_STEPS_COARSE:
      return 8;
    case MORPH_STEPS_MEDIUM:
      return 16;
    case MORPH_STEPS_FINE:
      return 32;
    case MORPH_STEPS_MANUAL:
      return (action->morph_manual_steps >= 8 && action->morph_manual_steps <= 128) ?
        action->morph_manual_steps : 32;
    default:
      return 16;
  }
}

void action_morph_update_timer(void) {
  if (!s_morph_timer) {
    ESP_LOGW(TAG, "Morph timer not initialized");
    return;
  }

  bool any_active = false;
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (s_active_morphs[i].active && !s_active_morphs[i].sync_waiting_final) {
      any_active = true;
      break;
    }
  }
  if (!any_active) any_active = action_boomerang_any_active();
  if (!any_active) any_active = s_tempo_morph.active;

  if (any_active) {
    if (!esp_timer_is_active(s_morph_timer)) {
      esp_timer_start_periodic(s_morph_timer, 10000);
    }
  } else {
    if (esp_timer_is_active(s_morph_timer)) {
      esp_timer_stop(s_morph_timer);
    }
  }
}

static void morph_send_final_values(active_morph_t* m) {
  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;

  for (int i = 0; i < m->num_ccs; i++) {
    uint8_t target = m->target_values[i];

    if (target != m->last_sent_values[i]) {
      send_control_change(channel, m->cc_numbers[i], target);
      action_set_cc_value(m->cc_numbers[i], target);
      m->last_sent_values[i] = target;
    }

    if (m->clear_dirty_on_complete)
      cc_state_clear_dirty(m->cc_numbers[i]);
  }

  ESP_LOGD(TAG, "Morph completed: CC%d -> %d (%u CCs)",
    m->cc_numbers[0], m->target_values[0], (unsigned)m->num_ccs);
}

static void morph_advance_step(active_morph_t* m) {
  if (!m->active) return;

  m->current_step++;

  if (m->current_step >= m->total_steps) {
    if (m->timing_mode == MORPH_TIMING_SYNC && m->sync_target_beat != 0) {
      m->sync_waiting_final = true;
      ESP_LOGD(TAG, "Morph waiting for beat %d to complete", m->sync_target_beat);
    } else {
      morph_send_final_values(m);
      m->active = false;
    }
    return;
  }

  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

  for (int i = 0; i < m->num_ccs; i++) {
    uint8_t new_value;

    const midi_control_t* ctrl = device ?
      assets_get_control_by_cc(device, m->cc_numbers[i]) : NULL;
    bool is_discrete = ctrl && ctrl->discrete_count > 0 && ctrl->discrete_values;

    if (is_discrete) {
      if (m->delay_final[i]) {
        new_value = m->start_values[i];
      } else {
        float progress = (float)m->current_step / m->total_steps;

        int start_idx = find_discrete_index(ctrl->discrete_values,
          ctrl->discrete_count, m->start_values[i]);
        int target_idx = find_discrete_index(ctrl->discrete_values,
          ctrl->discrete_count, m->target_values[i]);

        int idx_range = target_idx - start_idx;
        int current_idx = start_idx + (int)(idx_range * progress);

        if (current_idx < 0) current_idx = 0;
        if (current_idx >= ctrl->discrete_count) current_idx = ctrl->discrete_count - 1;

        new_value = (uint8_t)(ctrl->discrete_values[current_idx].value & 0x7F);
      }
    } else {
      int16_t start = m->start_values[i];
      int16_t target = m->target_values[i];
      int16_t range = target - start;

      int16_t value = start + (range * m->current_step) / m->total_steps;

      if (value < 0) value = 0;
      if (value > 127) value = 127;

      new_value = (uint8_t)value;
    }

    if (new_value != m->last_sent_values[i]) {
      send_control_change(channel, m->cc_numbers[i], new_value);
      action_set_cc_value(m->cc_numbers[i], new_value);
      m->last_sent_values[i] = new_value;
    }
  }
}

static void tempo_morph_tick(uint32_t now) {
  if (!s_tempo_morph.active) return;

  if (now >= s_tempo_morph.end_ms) {
    if (s_tempo_morph.target_bpm_x10 != s_tempo_morph.last_sent_bpm_x10)
      tempo_set_bpm_x10(s_tempo_morph.target_bpm_x10);
    s_tempo_morph.active = false;
    return;
  }

  uint32_t total = s_tempo_morph.end_ms - s_tempo_morph.start_ms;
  if (total == 0) total = 1;
  uint32_t elapsed = now - s_tempo_morph.start_ms;
  int32_t delta = (int32_t)s_tempo_morph.target_bpm_x10 -
    (int32_t)s_tempo_morph.start_bpm_x10;
  int32_t value = (int32_t)s_tempo_morph.start_bpm_x10 +
    (delta * (int32_t)elapsed) / (int32_t)total;
  if (value < (int32_t)TEMPO_MIN_BPM_X10) value = TEMPO_MIN_BPM_X10;
  if (value > (int32_t)TEMPO_MAX_BPM_X10) value = TEMPO_MAX_BPM_X10;
  uint16_t bpm_x10 = (uint16_t)value;
  if (bpm_x10 != s_tempo_morph.last_sent_bpm_x10) {
    tempo_set_bpm_x10(bpm_x10);
    s_tempo_morph.last_sent_bpm_x10 = bpm_x10;
  }
}

static void morph_timer_callback(void* arg) {
  (void)arg;
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

  bool any_morph_active = false;

  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    active_morph_t* m = &s_active_morphs[i];
    if (!m->active) continue;
    if (m->sync_waiting_final) continue;

    any_morph_active = true;

    if (now >= m->next_step_time) {
      morph_advance_step(m);
      m->next_step_time = now + m->step_interval_ms;
    }
  }

  action_boomerang_tick_all(now);
  tempo_morph_tick(now);

  if (!any_morph_active && !action_boomerang_any_active() && !s_tempo_morph.active) {
    action_morph_update_timer();
  }
}

static void morph_beat_event_handler(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_BEAT) return;

  uint8_t current_beat = event->data.beat.beat_in_bar;

  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    active_morph_t* m = &s_active_morphs[i];
    if (!m->active) continue;
    if (!m->sync_waiting_final) continue;

    bool hit_target = false;

    if (m->sync_target_beat == 0) {
      hit_target = true;
    } else if (m->sync_target_beat == current_beat) {
      hit_target = true;
    }

    if (hit_target) {
      morph_send_final_values(m);
      m->active = false;
      ESP_LOGD(TAG, "Morph SYNC completed on beat %d", current_beat);
    }
  }
}

static active_morph_t* find_or_create_morph_slot(uint8_t num_ccs,
    const uint8_t* cc_numbers) {
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (s_active_morphs[i].active) {
      for (int j = 0; j < s_active_morphs[i].num_ccs; j++) {
        for (int k = 0; k < num_ccs; k++) {
          if (s_active_morphs[i].cc_numbers[j] == cc_numbers[k]) {
            ESP_LOGD(TAG, "Canceling existing morph for CC%d", cc_numbers[k]);
            return &s_active_morphs[i];
          }
        }
      }
    }
  }

  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (!s_active_morphs[i].active) {
      return &s_active_morphs[i];
    }
  }

  ESP_LOGW(TAG, "No morph slot available");
  return NULL;
}

bool action_morph_start(const action_t* action, uint8_t num_ccs,
    const uint8_t* cc_numbers, const uint8_t* target_values,
    bool clear_dirty_on_complete) {
  if (!action->morph_enabled) return false;
  if (num_ccs == 0 || num_ccs > MORPH_MAX_CCS) return false;

  active_morph_t* m = find_or_create_morph_slot(num_ccs, cc_numbers);
  if (!m) return false;

  uint16_t bpm_x10 = tempo_get_bpm_x10();
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;

  time_signature_t sig = tempo_get_time_signature();
  uint8_t beats_per_bar = sig.numerator;
  if (beats_per_bar == 0) beats_per_bar = 4;

  uint8_t current_beat = transport_get_current_beat();
  if (current_beat == 0) current_beat = 1;

  uint32_t duration_ms;
  switch (action->morph_timing_mode) {
    case MORPH_TIMING_FEEL:
      duration_ms = get_feel_duration_ms(action->morph_feel, bpm_x10);
      break;
    case MORPH_TIMING_DURATION:
      duration_ms = action_morph_get_duration_ms(action->morph_division, bpm_x10);
      break;
    case MORPH_TIMING_SYNC:
      duration_ms = get_sync_duration_ms(action->morph_division, bpm_x10,
        current_beat, beats_per_bar);
      break;
    default:
      duration_ms = 500;
      break;
  }

  uint8_t max_delta = 0;
  for (int i = 0; i < num_ccs; i++) {
    uint8_t start = action_get_cc_value(cc_numbers[i]);
    uint8_t target = target_values[i];
    uint8_t delta = (start > target) ? (start - target) : (target - start);
    if (delta > max_delta) max_delta = delta;
  }

  uint8_t steps = get_morph_steps(action, max_delta, duration_ms);
  if (steps == 0) steps = 1;

  // Clamp so num_ccs * (1000 / step_interval) stays under ~800 msg/s. AUTO
  // already self-limits; FINE/MANUAL at the 10ms floor can otherwise flood
  // MIDI DIN (~1040 msg/s) and drop jobs from midi_out_queue.
  if (num_ccs > 0 && duration_ms > 0) {
    uint32_t max_steps_bw = (duration_ms * 800u) / ((uint32_t)num_ccs * 1000u);
    if (max_steps_bw < 4) max_steps_bw = 4;
    if (steps > max_steps_bw) steps = (uint8_t)max_steps_bw;
  }

  uint32_t step_interval = duration_ms / steps;
  if (step_interval < 10) step_interval = 10;

  m->active = true;
  m->action = (action_t*)action;
  m->num_ccs = num_ccs;
  m->current_step = 0;
  m->total_steps = steps;
  m->step_interval_ms = step_interval;
  m->timing_mode = action->morph_timing_mode;
  m->sync_target_beat = (action->morph_timing_mode == MORPH_TIMING_SYNC) ?
    get_sync_target_beat(action->morph_division) : 0;
  m->sync_waiting_final = false;
  m->clear_dirty_on_complete = clear_dirty_on_complete;

  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

  for (int i = 0; i < num_ccs; i++) {
    m->cc_numbers[i] = cc_numbers[i];
    m->start_values[i] = action_get_cc_value(cc_numbers[i]);
    m->target_values[i] = target_values[i];
    m->last_sent_values[i] = m->start_values[i];

    const midi_control_t* ctrl = device ?
      assets_get_control_by_cc(device, cc_numbers[i]) : NULL;
    m->delay_final[i] = (ctrl && ctrl->discrete_count > 0 &&
      ctrl->discrete_count <= 4);
  }

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  m->next_step_time = now + step_interval;

  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;
  for (int i = 0; i < num_ccs; i++) {
    send_control_change(channel, cc_numbers[i], m->start_values[i]);
    m->last_sent_values[i] = m->start_values[i];
  }

  action_morph_update_timer();

  ESP_LOGD(TAG, "Morph started: CC%d %d->%d, %d CCs, %d steps, %lu ms interval",
    cc_numbers[0], m->start_values[0], m->target_values[0],
    (int)num_ccs, (int)steps, (unsigned long)step_interval);

  return true;
}

// Compute the duration in ms for an action's morph configuration. Used by
// both CC and tempo morph paths so the three timing modes behave the same.
uint32_t action_morph_compute_duration_ms(const action_t* action) {
  uint16_t bpm_x10 = tempo_get_bpm_x10();
  if (bpm_x10 == 0) bpm_x10 = TEMPO_DEFAULT_BPM_X10;
  switch (action->morph_timing_mode) {
    case MORPH_TIMING_FEEL:
      return get_feel_duration_ms(action->morph_feel, bpm_x10);
    case MORPH_TIMING_DURATION:
      return action_morph_get_duration_ms(action->morph_division, bpm_x10);
    case MORPH_TIMING_SYNC: {
      time_signature_t sig = tempo_get_time_signature();
      uint8_t beats_per_bar = sig.numerator ? sig.numerator : 4;
      uint8_t current_beat = transport_get_current_beat();
      if (current_beat == 0) current_beat = 1;
      return get_sync_duration_ms(action->morph_division, bpm_x10,
        current_beat, beats_per_bar);
    }
    default:
      return 500;
  }
}

bool action_tempo_morph_start(uint16_t target_bpm_x10, uint32_t duration_ms) {
  uint16_t current = tempo_get_bpm_x10();
  if (current == 0) current = TEMPO_DEFAULT_BPM_X10;
  if (target_bpm_x10 < TEMPO_MIN_BPM_X10) target_bpm_x10 = TEMPO_MIN_BPM_X10;
  if (target_bpm_x10 > TEMPO_MAX_BPM_X10) target_bpm_x10 = TEMPO_MAX_BPM_X10;

  if (duration_ms < 10 || current == target_bpm_x10) {
    if (current != target_bpm_x10) tempo_set_bpm_x10(target_bpm_x10);
    s_tempo_morph.active = false;
    action_morph_update_timer();
    return true;
  }

  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  s_tempo_morph.active = true;
  s_tempo_morph.start_bpm_x10 = current;
  s_tempo_morph.target_bpm_x10 = target_bpm_x10;
  s_tempo_morph.start_ms = now;
  s_tempo_morph.end_ms = now + duration_ms;
  s_tempo_morph.last_sent_bpm_x10 = current;
  action_morph_update_timer();

  char cur_buf[16], tgt_buf[16];
  tempo_format_bpm(cur_buf, sizeof(cur_buf), current);
  tempo_format_bpm(tgt_buf, sizeof(tgt_buf), target_bpm_x10);
  ESP_LOGD(TAG, "Tempo morph: %s -> %s BPM over %lu ms",
    cur_buf, tgt_buf, (unsigned long)duration_ms);
  return true;
}

void action_morph_clear(void) {
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    s_active_morphs[i].active = false;
  }
  s_tempo_morph.active = false;
  action_morph_update_timer();
  ESP_LOGD(TAG, "Cleared all active morphs");
}

// Public API wrapper for header compatibility
void action_clear_morphs(void) {
  action_morph_clear();
}

esp_err_t action_morph_init(void) {
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    s_active_morphs[i].active = false;
  }

  esp_timer_create_args_t morph_timer_args = {
    .callback = morph_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "morph"
  };
  esp_err_t ret = esp_timer_create(&morph_timer_args, &s_morph_timer);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create morph timer: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = event_bus_subscribe_named(EVENT_BEAT, morph_beat_event_handler, NULL,
    "action_morph.beat_sync");
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to beat events for morph: %s", esp_err_to_name(ret));
  }

  return ESP_OK;
}
