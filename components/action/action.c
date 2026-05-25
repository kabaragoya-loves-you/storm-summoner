#include "action.h"
#include "action_internal.h"
#include "device_config.h"
#include "scene.h"
#include "config.h"
#include "event_bus.h"
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

uint8_t action_get_cc_value(uint8_t cc_num) {
  if (cc_num >= 128) return 0;
  return s_last_cc_values[cc_num];
}

void action_set_cc_value(uint8_t cc_num, uint8_t value) {
  if (cc_num >= 128) return;
  s_last_cc_values[cc_num] = value;
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

  action_punch_in_init();
  action_boomerang_init();
  action_morph_init();
  action_clock_burst_init();
  action_scheduler_init();

  s_initialized = true;
  return ESP_OK;
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
                      action->timing != ACTION_TIMING_IMMEDIATE;

  if (should_queue) {
    uint8_t target_beat = (action->timing == ACTION_TIMING_NEXT_BEAT)
      ? 0 : action->timing_beat;

    if (action_scheduler_enqueue(mutable_action, trigger_value, target_beat,
                                 repeats, 1)) {
      return ESP_OK;
    }
    // Fall through to immediate execution if queue is full
  }

  // Immediate+Repeat: fire now and arm the scheduler for periodic re-fires.
  // First scheduled fire is one full interval after the press, so the
  // pacing is consistent regardless of where in the bar the press landed.
  // Without this, start_repeating() above just adds the action to a tracker
  // list and nothing in the queue ever re-fires it.
  if (repeats && action->timing == ACTION_TIMING_IMMEDIATE && supports_timing) {
    scene_t* scene = scene_get_current();
    uint8_t beats_per_bar = (scene && scene->time_signature.numerator)
      ? scene->time_signature.numerator : 4;
    uint8_t interval = action_repeat_division_to_beats(
      action->repeat_division, beats_per_bar);
    if (interval == 0) interval = 1;  // sub-beat divisions: every beat
    action_scheduler_enqueue(mutable_action, trigger_value,
                             /*target_beat=*/0, /*repeating=*/true, interval);
  }

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
  action.params.tempo.bpm = bpm;
  return action;
}

action_t action_create_transport(action_type_t transport_type) {
  action_t action = {0};
  action.type = transport_type;
  return action;
}

action_t action_create_reset(void) {
  action_t action = {0};
  action.type = ACTION_RESET;
  return action;
}

action_t action_create_sustain(void) {
  action_t action = {0};
  action.type = ACTION_SUSTAIN;
  return action;
}

action_t action_create_sostenuto(void) {
  action_t action = {0};
  action.type = ACTION_SOSTENUTO;
  return action;
}

