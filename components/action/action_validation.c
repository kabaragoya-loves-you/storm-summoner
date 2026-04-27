#include "action_internal.h"
#include "scene.h"
#include "esp_log.h"

static const char* TAG = "action_validation";

// Actions that require press/release (hold) behavior
// These should NOT be assigned to bump or on_load
static const action_type_t hold_actions[] = {
  ACTION_CONTROL_HOLD,
  ACTION_PRESET_HOLD,
  ACTION_TEMPO_HOLD,
  ACTION_NOTE,
  ACTION_TOUCHWHEEL_HOLD,
  ACTION_SUSTAIN,
  ACTION_SOSTENUTO,
  ACTION_LFO_TOGGLE,  // Toggle needs discrete press
  ACTION_LFO_SHAPE,   // Shape cycle needs discrete press
  ACTION_CLOCK_HOLD,
  ACTION_CLOCK_BURST,
  ACTION_CUT_HOLD,
  ACTION_UI_HOLD,
  ACTION_PARAM_HOLD,
  ACTION_RTG_HOLD,
  ACTION_SAMPLE_HOLD_HOLD,
};

bool action_requires_hold(action_type_t type) {
  for (size_t i = 0; i < sizeof(hold_actions) / sizeof(hold_actions[0]); i++) {
    if (hold_actions[i] == type) return true;
  }
  return false;
}

// Enforces restrictions for touchwheel mode actions and hold actions
bool action_is_valid_for_trigger(action_type_t type, action_trigger_type_t trigger) {
  if (type == ACTION_NONE) return true;

  // Hold actions are invalid for bump, on_load, and on_play (no release event)
  if (action_requires_hold(type)) {
    if (trigger == ACTION_TRIGGER_BUMP || trigger == ACTION_TRIGGER_ON_LOAD ||
        trigger == ACTION_TRIGGER_ON_PLAY) {
      return false;
    }
  }

  // Transport actions cannot be assigned to on_play (would cause recursion/conflicts)
  if (trigger == ACTION_TRIGGER_ON_PLAY) {
    if (type == ACTION_PLAY || type == ACTION_STOP ||
        type == ACTION_PAUSE || type == ACTION_RECORD) {
      return false;
    }
  }

  // Touchwheel Hold restrictions:
  // - Valid: Pads 8-11, Buttons, Expression switch
  // - Invalid: Pads 0-7, Bump, on_load, on_play
  if (type == ACTION_TOUCHWHEEL_HOLD) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }

  // Touchwheel Cycle restrictions:
  // - Valid: Pads 8-11, Buttons, Bump, Expression switch
  // - Invalid: Pads 0-7, on_load, on_play
  if (type == ACTION_TOUCHWHEEL_CYCLE) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_BUMP:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }

  // UI actions cannot be assigned to on_load or on_play
  if (type == ACTION_SET_UI || type == ACTION_UI_CYCLE) {
    if (trigger == ACTION_TRIGGER_ON_LOAD || trigger == ACTION_TRIGGER_ON_PLAY) return false;
  }

  // LFO start/stop cannot be assigned to on_load (LFOs auto-start based on config)
  // but ARE allowed for on_play
  if (type == ACTION_LFO_START || type == ACTION_LFO_STOP) {
    if (trigger == ACTION_TRIGGER_ON_LOAD) return false;
  }

  // Param Hold restrictions:
  // - Valid: Pads 8-11, Buttons, Expression switch
  // - Invalid: Pads 0-7, Bump, on_load, on_play
  if (type == ACTION_PARAM_HOLD) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }

  // Param Cycle restrictions:
  // - Valid: Pads 8-11, Buttons, Bump, Expression switch
  // - Invalid: Pads 0-7, on_load, on_play
  if (type == ACTION_PARAM_CYCLE) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_BUMP:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }

  return true;
}

// Returns true for actions that support timing options (non-HOLD actions)
// HOLD actions must execute immediately to preserve press/release pairing
// TAP_TEMPO is always immediate (toggles tap mode instantly)
// PUNCH_IN has built-in timing (always starts at next bar)
bool action_supports_timing(action_type_t type) {
  if (type == ACTION_NONE || type == ACTION_TAP_TEMPO || type == ACTION_PUNCH_IN) return false;
  return !action_requires_hold(type);
}

// Preset/scene actions support timing but NOT repeat
// TAP_TEMPO never repeats (it's a mode toggle)
bool action_supports_repeat(action_type_t type) {
  if (type == ACTION_NONE || action_requires_hold(type)) return false;
  switch (type) {
    case ACTION_PRESET:
    case ACTION_PRESET_INC:
    case ACTION_PRESET_DEC:
    case ACTION_PRESET_CYCLE:
    case ACTION_SCENE:
    case ACTION_SCENE_INC:
    case ACTION_SCENE_DEC:
    case ACTION_TAP_TEMPO:
    case ACTION_SET_UI:
    case ACTION_STEP:
    case ACTION_RTG_TOGGLE:
    case ACTION_SAMPLE_HOLD_TOGGLE:
    case ACTION_PUNCH_IN:
      return false;
    default:
      return true;
  }
}

bool action_supports_transport_trigger(action_type_t type) {
  return action_supports_timing(type) && action_supports_repeat(type);
}

bool action_supports_morph(action_type_t type) {
  switch (type) {
    case ACTION_CONTROL_HOLD:
    case ACTION_CONTROL_CYCLE:
    case ACTION_RANDOMIZE:
      return true;
    default:
      return false;
  }
}

bool action_supports_raise_flag(action_type_t type) {
  switch (type) {
    case ACTION_PLAY:
    case ACTION_STOP:
    case ACTION_PAUSE:
    case ACTION_RECORD:
    case ACTION_CONTROL_CHANGE:
    case ACTION_CONTROL_HOLD:
    case ACTION_CONTROL_CYCLE:
    case ACTION_NOTE:
    case ACTION_RANDOMIZE:
    case ACTION_PUNCH_IN:
    case ACTION_FLAG_CEREMONY:
    case ACTION_BOOMERANG:
      return true;
    default:
      return false;
  }
}

bool action_validate_timing(action_t* action, uint8_t beats_per_bar) {
  if (!action) return false;
  if (action->timing != ACTION_TIMING_SPECIFIC_BEAT) return false;
  if (action->timing_beat <= beats_per_bar) return false;

  ESP_LOGW(TAG, "Beat %d invalid for %d-beat bar, remapping to beat 1",
    action->timing_beat, beats_per_bar);
  action->timing_beat = 1;
  return true;
}

void action_validate_scene_timings(scene_t* scene) {
  if (!scene) return;

  uint8_t beats = scene->time_signature.numerator;
  if (beats == 0) beats = 4;

  int remapped = 0;

  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    if (action_validate_timing(&scene->touchpads[i].action, beats)) remapped++;
  }

  if (action_validate_timing(&scene->button_left, beats)) remapped++;
  if (action_validate_timing(&scene->button_right, beats)) remapped++;
  if (action_validate_timing(&scene->button_both, beats)) remapped++;

  if (action_validate_timing(&scene->bump, beats)) remapped++;

  for (int i = 0; i < scene->num_on_load_actions && i < MAX_ON_LOAD_ACTIONS; i++) {
    if (action_validate_timing(&scene->on_load[i], beats)) remapped++;
  }

  if (action_validate_timing(&scene->sustain, beats)) remapped++;
  if (action_validate_timing(&scene->sostenuto, beats)) remapped++;
  if (action_validate_timing(&scene->expr_switch, beats)) remapped++;

  if (remapped > 0) {
    ESP_LOGW(TAG, "Remapped %d action(s) with invalid beat timings to beat 1", remapped);
  }
}
