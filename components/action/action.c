#include "action.h"
#include "action_internal.h"
#include "action_note_hold.h"
#include "device_config.h"
#include "scene.h"
#include "tempo.h"
#include "config.h"
#include "event_bus.h"
#include "assets_manager.h"
#include "midi_in.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "action";

static bool s_initialized = false;

// ============================================================================
// CC value cache (shared across action modules via action_get/set_cc_value)
// ============================================================================
static uint8_t s_last_cc_values[128];

// ============================================================================
// Scene-local flag (semaphore) - reset on scene change
// ============================================================================
static uint8_t s_scene_flag = 0;

// Optional UI observer for gating-CC changes driven by the incoming-CC mirror.
static action_gating_changed_cb_t s_gating_changed_observer = NULL;

void action_set_gating_changed_observer(action_gating_changed_cb_t cb) {
  s_gating_changed_observer = cb;
}

uint8_t action_get_cc_value(uint8_t cc_num) {
  if (cc_num >= 128) return 0;
  return s_last_cc_values[cc_num];
}

// When a gating CC changes, re-clamp the cached values of CCs that carry
// x_variants so the live cache stays valid for the new mode's effective range
// (e.g. a 0-127 value left over from Delay mode is snapped into Loop mode's
// 0-1 range). Touches only s_last_cc_values, never stored scene data.
static void action_clamp_dependents_for_gating(const device_def_t* dev) {
  for (uint16_t i = 0; i < dev->control_count; i++) {
    const midi_control_t* ctrl = &dev->controls[i];
    if (ctrl->type != MIDI_CONTROL_TYPE_CC || ctrl->id >= 128) continue;
    if (ctrl->variant_count == 0) continue;  // only mode-dependent CCs

    uint8_t cur = s_last_cc_values[ctrl->id];
    uint8_t clamped = (uint8_t)assets_clamp_cc_value(dev, ctrl->id, cur);
    if (assets_cc_has_discrete_values(dev, ctrl->id))
      clamped = (uint8_t)assets_snap_to_discrete(dev, ctrl->id, clamped);
    if (clamped != cur)
      s_last_cc_values[ctrl->id] = clamped;
  }
}

void action_set_cc_value(uint8_t cc_num, uint8_t value) {
  if (cc_num >= 128) return;
  uint8_t prev = s_last_cc_values[cc_num];
  s_last_cc_values[cc_num] = value;
  if (prev == value) return;

  // A gating CC crossing a variant boundary can invalidate dependent CCs'
  // cached values; re-clamp them against the new effective ranges.
  const device_def_t* dev =
    (const device_def_t*)scene_get_device(scene_get_current_index());
  if (dev && assets_cc_is_gating(dev, cc_num))
    action_clamp_dependents_for_gating(dev);
}

// Mirror incoming CC into the live value cache so mode-gating CCs set by an
// external controller drive x_variants resolution. Gated by config_get_cc_mirror
// (default off) and filtered to the active device's MIDI channel. Runs on the
// event-dispatcher task (not ISR), so action_set_cc_value is safe to call.
static void action_cc_mirror_handler(const event_t* event, void* context) {
  (void)context;
  if (!config_get_cc_mirror()) return;
  if (!event || event->type != EVENT_MIDI_IN) return;
  if (event->data.midi_in.type != MIDI_EVENT_CONTROL_CHANGE) return;

  uint8_t our_channel = scene_get_effective_channel(scene_get_current_index());
  if (our_channel < 1 || our_channel > 16) return;
  if (event->data.midi_in.channel != (uint8_t)(our_channel - 1)) return;

  uint8_t cc = event->data.midi_in.data1;
  uint8_t prev = s_last_cc_values[cc < 128 ? cc : 0];
  action_set_cc_value(cc, event->data.midi_in.data2);

  // Runs on the event-dispatcher task (same context as button nav), so it is
  // safe to drive a UI roller refresh when an external controller changed a
  // gating CC's value.
  if (s_gating_changed_observer && cc < 128 && prev != event->data.midi_in.data2) {
    const device_def_t* dev =
      (const device_def_t*)scene_get_device(scene_get_current_index());
    if (dev && assets_cc_is_gating(dev, cc))
      s_gating_changed_observer(cc);
  }
}

void action_reset_cc_values(const void* device) {
  const device_def_t* dev = (const device_def_t*)device;

  if (!dev || dev->control_count == 0) {
    memset(s_last_cc_values, 0, sizeof(s_last_cc_values));
    return;
  }

  memset(s_last_cc_values, 0, sizeof(s_last_cc_values));

  for (uint16_t i = 0; i < dev->control_count; i++) {
    const midi_control_t* ctrl = &dev->controls[i];
    if (ctrl->type == MIDI_CONTROL_TYPE_CC && ctrl->id < 128) {
      s_last_cc_values[ctrl->id] = (uint8_t)ctrl->min;
    }
  }
}

void action_clear_flag(void) {
  s_scene_flag = 0;
  ESP_LOGD(TAG, "Scene flag cleared");
}

uint8_t action_get_flag(void) {
  return s_scene_flag;
}

void action_set_flag(uint8_t value) {
  s_scene_flag = value ? 1 : 0;
  ESP_LOGD(TAG, "Scene flag set to %d", s_scene_flag);
}

// ============================================================================
// Initialization
// ============================================================================
esp_err_t action_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Action system already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing action system");

  memset(s_last_cc_values, 64, sizeof(s_last_cc_values));
  s_scene_flag = 0;

  // Let assets_manager resolve x_variants against the live CC cache.
  assets_set_cc_value_provider(action_get_cc_value);

  // Mirror incoming CC into s_last_cc_values when enabled (default off).
  event_bus_subscribe(EVENT_MIDI_IN, action_cc_mirror_handler, NULL);

  action_punch_in_init();
  action_boomerang_init();
  action_morph_init();
  action_clock_burst_init();
  action_scheduler_init();
  action_note_hold_init();

  s_initialized = true;
  return ESP_OK;
}

static bool action_queue_timed(action_t* action, uint8_t trigger_value, bool repeats,
    bool include_transport_start) {
  bool supports_timing = action_supports_timing_for(action);
  if (!supports_timing || action->timing == ACTION_TIMING_IMMEDIATE)
    return false;

  uint8_t target_beat = 0;
  uint16_t target_bars = 0;

  if (action->timing == ACTION_TIMING_BAR) {
    target_bars = (uint16_t)action->timing_beat;
  } else if (action->timing == ACTION_TIMING_TRANSPORT_START) {
    if (!include_transport_start) return false;
    target_beat = 1;
  } else {
    target_beat = (action->timing == ACTION_TIMING_NEXT_BEAT)
      ? 0 : action->timing_beat;
  }

  return action_scheduler_enqueue(action, trigger_value, target_beat,
    target_bars, repeats, 1);
}

static void action_queue_immediate_repeat(action_t* action, uint8_t trigger_value) {
  if (!action->repeat_enabled || !action_supports_repeat_for(action)) return;
  if (!action_supports_timing_for(action)) return;
  if (action->timing != ACTION_TIMING_IMMEDIATE) return;

  scene_t* scene = scene_get_current();
  uint8_t beats_per_bar = (scene && scene->time_signature.numerator)
    ? scene->time_signature.numerator : 4;
  uint8_t interval = action_repeat_division_to_beats(
    action->repeat_division, beats_per_bar);
  if (interval == 0) interval = 1;
  action_scheduler_enqueue(action, trigger_value, 0, 0, true, interval);
}

esp_err_t action_execute_triggered(const action_t* action, uint8_t trigger_value) {
  if (!action || action->type == ACTION_NONE) return ESP_OK;
  action_t* mutable_action = (action_t*)action;

  action_scheduler_prepare_trigger(mutable_action);

  bool repeats = action->repeat_enabled && action_supports_repeat_for(action);
  if (repeats)
    action_scheduler_start_repeating(mutable_action);

  if (action_queue_timed(mutable_action, trigger_value, repeats, true))
    return ESP_OK;

  if (repeats)
    action_queue_immediate_repeat(mutable_action, trigger_value);

  return action_execute_immediate(action, trigger_value, true);
}

// ============================================================================
// Public execute entry point - handles timing, queuing, and repeat toggle
// ============================================================================
esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press) {
  if (!action || action->type == ACTION_NONE) return ESP_OK;

  action_t* mutable_action = (action_t*)action;

  // HOLD action release for repeating actions: mark as released so the
  // scheduler stops after the pending fire.
  if (!is_press && action_requires_hold_for(action) && action->repeat_enabled) {
    action_scheduler_mark_hold_released(mutable_action);
    return action_execute_immediate(action, trigger_value, is_press);
  }

  // On-Transport timing: pad press arms; transport start fires when armed.
  if (is_press && action->timing == ACTION_TIMING_TRANSPORT_START) {
    action_scheduler_transport_pad_press(mutable_action);
    return ESP_OK;
  }

  // Non-HOLD repeating actions use toggle-on-press behavior.
  bool repeats = is_press && action->repeat_enabled &&
                 action_supports_repeat_for(action);
  if (repeats) {
    if (action_scheduler_is_repeating(mutable_action)) {
      action_scheduler_stop_repeating(mutable_action);
      ESP_LOGD(TAG, "Stopped repeating action (toggle off)");
      return ESP_OK;
    }
    action_scheduler_start_repeating(mutable_action);
  }

  bool supports_timing = action_supports_timing_for(action);
  bool should_queue = is_press && supports_timing &&
                      action->timing != ACTION_TIMING_IMMEDIATE &&
                      action->timing != ACTION_TIMING_TRANSPORT_START;

  if (should_queue && action_queue_timed(mutable_action, trigger_value, repeats, false))
    return ESP_OK;

  if (repeats && action->timing == ACTION_TIMING_IMMEDIATE && supports_timing)
    action_queue_immediate_repeat(mutable_action, trigger_value);

  return action_execute_immediate(action, trigger_value, is_press);
}

// ============================================================================
// Immediate dispatch - runs the chained handlers and common post-processing
// ============================================================================
esp_err_t action_execute_immediate(const action_t* action, uint8_t trigger_value, bool is_press) {
  if (!action || action->type == ACTION_NONE) return ESP_OK;

  ESP_LOGD(TAG, "Executing action: %s (trigger=%d, press=%d)",
    action_type_name(action->type), trigger_value, is_press);

  uint8_t channel = scene_get_effective_channel(scene_get_current_index()) - 1;

  action_handle_result_t result =
    action_handlers_midi_dispatch(action, trigger_value, is_press, channel);
  if (result == ACTION_NOT_HANDLED)
    result = action_handlers_scene_dispatch(action, trigger_value, is_press, channel);
  if (result == ACTION_NOT_HANDLED)
    result = action_handlers_modulation_dispatch(action, trigger_value, is_press, channel);

  if (result == ACTION_NOT_HANDLED) {
    ESP_LOGW(TAG, "Unhandled action type: %d", action->type);
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (result == ACTION_HANDLED_SKIP_FLAG) return ESP_OK;

  if (is_press && config_get_flag_enabled() && action->raise_flag) {
    s_scene_flag = 1;
    ESP_LOGD(TAG, "Raise the Flag: flag set to 1 after action %s",
      action_type_name(action->type));
  }

  if (is_press) {
    event_t evt = {
      .type = EVENT_ACTION_EXECUTED,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp(),
      .data.action_executed = {
        .source_type = 255,
        .source_index = 0,
        .action_type = action->type,
        .action_variant = action->variant,
        .cc_number = 0,
        .cc_value = 0,
        .note = 0,
        .velocity = 0
      }
    };

    if (action->type == ACTION_CONTROL) {
      if (action->params.control.num_ccs > 0) {
        evt.data.action_executed.cc_number = action->params.control.cc_numbers[0];
        evt.data.action_executed.cc_value = action->params.control.values[0];
        if (action->variant == VARIANT_HOLD) {
          evt.data.action_executed.cc_value2 = action->params.control.values2[0];
        }
      }
    } else if (action->type == ACTION_NOTE) {
      evt.data.action_executed.note = action->params.note.note;
      evt.data.action_executed.velocity = action->params.note.velocity;
    }

    event_bus_post(&evt);
  }

  return ESP_OK;
}

// ============================================================================
// Action creator helpers
// ============================================================================
action_t action_create_control(uint8_t cc_number, uint8_t value) {
  action_t action = {0};
  action.type = ACTION_CONTROL;
  action.variant = VARIANT_SET;
  action.params.control.num_ccs = 1;
  action.params.control.cc_numbers[0] = cc_number;
  action.params.control.values[0] = value;
  return action;
}

action_t action_create_preset_inc(void) {
  action_t action = {0};
  action.type = ACTION_PRESET;
  action.variant = VARIANT_INCREMENT;
  return action;
}

action_t action_create_preset_dec(void) {
  action_t action = {0};
  action.type = ACTION_PRESET;
  action.variant = VARIANT_DECREMENT;
  return action;
}

action_t action_create_scene_inc(void) {
  action_t action = {0};
  action.type = ACTION_SCENE;
  action.variant = VARIANT_INCREMENT;
  return action;
}

action_t action_create_scene_dec(void) {
  action_t action = {0};
  action.type = ACTION_SCENE;
  action.variant = VARIANT_DECREMENT;
  return action;
}

action_t action_create_tap_tempo(void) {
  action_t action = {0};
  action.type = ACTION_TEMPO;
  action.variant = VARIANT_TAP;
  return action;
}

action_t action_create_set_tempo(uint16_t bpm) {
  action_t action = {0};
  action.type = ACTION_TEMPO;
  action.variant = VARIANT_SET;
  action.params.tempo.bpm = tempo_whole_to_x10(bpm);
  return action;
}

action_t action_create_transport(action_variant_t variant) {
  action_t action = {0};
  action.type = ACTION_TRANSPORT;
  action.variant = variant;
  return action;
}

action_t action_create_reset(void) {
  action_t action = {0};
  action.type = ACTION_RESET;
  return action;
}

action_t action_create_inspect_scene(void) {
  action_t action = {0};
  action.type = ACTION_INSPECT_SCENE;
  return action;
}

action_t action_create_piano_pedal(uint8_t cc_number) {
  action_t action = {0};
  action.type = ACTION_PIANO_PEDAL;
  // Whitelist the five standard piano pedal CCs; anything else collapses
  // to Damper (CC 64). Keeps the menu picker and the runtime in sync --
  // the picker only offers these five, and a bad value from console or
  // legacy JSON falls back to the most common pedal rather than firing a
  // surprise CC.
  switch (cc_number) {
    case 64: case 66: case 67: case 68: case 69:
      action.params.piano_pedal.cc_number = cc_number;
      break;
    default:
      action.params.piano_pedal.cc_number = 64;
      break;
  }
  return action;
}

