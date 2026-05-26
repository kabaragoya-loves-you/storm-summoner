#include "action_internal.h"
#include "scene.h"
#include "esp_log.h"

static const char* TAG = "action_validation";

// Actions whose every (type, variant) combination requires press/release
// (hold) behavior. These should NOT be assigned to bump, on_load, or on_play.
// Consolidated families like ACTION_TEMPO are absent from this list because
// only SOME variants are hold-like (e.g. VARIANT_HOLD); callers with access
// to the full action_t should use action_requires_hold_for() for a precise
// answer.
static const action_type_t hold_actions[] = {
  ACTION_NOTE,
  ACTION_PIANO_PEDAL,
  // ACTION_LFO is intentionally NOT in this list. None of its variants are
  // press/release pairs (START/STOP/TOGGLE/MODIFY are all press-only). The
  // per-variant timing/repeat restrictions for TOGGLE live in the
  // action_supports_timing_for / action_supports_repeat_for carve-outs.
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

bool action_requires_hold_for(const action_t* action) {
  if (!action) return false;
  if (action_requires_hold(action->type)) return true;
  if (action->type == ACTION_TEMPO && action->variant == VARIANT_HOLD) return true;
  if (action->type == ACTION_CONTROL && action->variant == VARIANT_HOLD) return true;
  if (action->type == ACTION_PRESET && action->variant == VARIANT_HOLD) return true;
  if (action->type == ACTION_TOUCHWHEEL && action->variant == VARIANT_HOLD) return true;
  return false;
}

// Hold actions where it's safe (and musically useful) to gate the release
// phase on hold duration. Excludes:
//   - NOTE / SUSTAIN / SOSTENUTO -- the release IS the NoteOff / CC=0
//     pair; suppressing it strands a note or pedal.
//   - LFO variants / CLOCK_BURST -- not symmetric press/release pairs
//     (start/stop/toggle/modify and one-shot burst all fire on press only).
bool action_supports_followup_for(const action_t* action) {
  if (!action) return false;
  switch (action->type) {
    case ACTION_CLOCK_HOLD:
    case ACTION_CUT_HOLD:
    case ACTION_UI_HOLD:
    case ACTION_PARAM_HOLD:
    case ACTION_RTG_HOLD:
    case ACTION_SAMPLE_HOLD_HOLD:
      return true;
    case ACTION_TEMPO:
    case ACTION_CONTROL:
    case ACTION_PRESET:
    case ACTION_TOUCHWHEEL:
      return action->variant == VARIANT_HOLD;
    default:
      return false;
  }
}

// ============================================================================
// Trigger capability table + variant-aware validator
// ============================================================================
//
// One source of truth for "what each trigger can do" -- replaces the patchwork
// of is_on_load_allowed / is_on_play_allowed / exclude_hold_actions / per-type
// inline checks. The validator combines the trigger's capabilities with the
// action's (type, variant) to give a single yes/no answer; menu, runtime,
// JSON load, and console parsers all call the same function.

trigger_capabilities_t action_trigger_capabilities(action_trigger_type_t trigger) {
  trigger_capabilities_t caps = {
    .delivers_release   = false,
    .inhibits_transport = false,
    .fires_at_load_time = false,
    .fires_at_play_time = false,
  };
  switch (trigger) {
    case ACTION_TRIGGER_TOUCHPAD_0_7:
    case ACTION_TRIGGER_TOUCHPAD_8_11:
    case ACTION_TRIGGER_BUTTON:
    case ACTION_TRIGGER_EXPR_SWITCH:
      caps.delivers_release = true;
      break;
    case ACTION_TRIGGER_BUMP:
      // one-shot, no release pair
      break;
    case ACTION_TRIGGER_ON_LOAD:
      caps.fires_at_load_time = true;
      break;
    case ACTION_TRIGGER_ON_PLAY:
      caps.fires_at_play_time = true;
      caps.inhibits_transport = true;
      break;
  }
  return caps;
}

bool action_is_transport(action_type_t type) {
  return type == ACTION_TRANSPORT;
}

// Fire-and-forget category: actions that send a thing once and return,
// without needing a release pair, mode interaction, or live scene state.
// Encodes what the old is_on_load_allowed / is_on_play_allowed menu
// whitelists conveyed -- but variant-aware. Used by Rule 3 of the
// validator to gate ON_LOAD / ON_PLAY triggers.
bool action_is_fire_and_forget_for(const action_t* action) {
  if (!action) return false;
  switch (action->type) {
    // Consolidated families: only SET is one-shot. HOLD needs a release pair,
    // CYCLE/INC/DEC need per-press semantics (and on ON_LOAD/ON_PLAY would
    // silently advance state on every load), TAP needs mode interaction.
    case ACTION_CONTROL:
    case ACTION_TEMPO:
    case ACTION_PRESET:
    case ACTION_SCENE:
      return action->variant == VARIANT_SET;

    // Pure one-shots (transport family already collapsed -- all four
    // variants are fire-and-forget; no variant-level carve-out needed).
    case ACTION_TRANSPORT:
    case ACTION_RANDOMIZE:
    case ACTION_RESET:
    case ACTION_BOOMERANG:
    // ACTION_LFO -- all variants (START/STOP/TOGGLE/MODIFY) are press-only
    // one-shots, so fire-and-forget at the category level. Rule 4 of the
    // validator additionally rejects START/STOP/TOGGLE on ON_LOAD (LFOs
    // auto-start from scene config), but MODIFY and ON_PLAY are fine.
    case ACTION_LFO:
      return true;

    default:
      return false;
  }
}

// Action-specific input restrictions -- the rules that aren't reducible to
// "trigger has capability X". These are genuine per-action hardware
// affordance requirements (touchwheel mode actions only on touchwheel/button
// inputs; param hold/cycle same constraint).
static bool action_input_restriction_allows(action_type_t type,
                                            action_trigger_type_t trigger) {
  // Touchwheel (HOLD / CYCLE): hardware-input gate. Allow the touchwheel-
  // adjacent triggers; the HOLD variant's "needs a release pair" requirement
  // is enforced separately by Rule 1 (action_requires_hold_for + caps
  // .delivers_release), so HOLD on BUMP is already blocked upstream without
  // a variant check here.
  if (type == ACTION_TOUCHWHEEL) {
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
  // Param Hold / Cycle mirror the touchwheel rules (they manipulate the
  // touchwheel's CC slot 1, so they need similar input affordances).
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
  // UI module changes only make sense from a live input (the user needs to
  // see the UI swap in response to their input). Already handled by the
  // fire-and-forget gate for ON_LOAD/ON_PLAY -- this is the live-input gate.
  // (No additional rule needed; SET_UI/UI_CYCLE aren't fire-and-forget.)
  return true;
}

// Canonical variant-aware validator. All call sites that have a real
// action_t should use this. The by-type wrapper below builds a synthetic
// action_t for legacy callers.
bool action_is_valid_for_trigger_for(const action_t* action,
                                     action_trigger_type_t trigger) {
  if (!action || action->type == ACTION_NONE) return true;

  trigger_capabilities_t caps = action_trigger_capabilities(trigger);

  // Rule 1: HOLD variants need a release event from the trigger.
  if (action_requires_hold_for(action) && !caps.delivers_release) return false;

  // Rule 2: transport recursion guard.
  if (caps.inhibits_transport && action_is_transport(action->type)) return false;

  // Rule 3: load/play-time triggers only accept fire-and-forget actions.
  if ((caps.fires_at_load_time || caps.fires_at_play_time)
      && !action_is_fire_and_forget_for(action)) {
    return false;
  }

  // Rule 4: ACTION_LFO START/STOP/TOGGLE rejected on ON_LOAD (LFOs auto-start
  //   from scene config; firing them here would race the init path). MODIFY
  //   is allowed because parameter overrides on a not-yet-running engine are
  //   harmless. ON_PLAY is fine for all variants because the scene is already
  //   live by then.
  if (caps.fires_at_load_time && action->type == ACTION_LFO) {
    if (action->variant == VARIANT_START ||
        action->variant == VARIANT_STOP ||
        action->variant == VARIANT_TOGGLE) {
      return false;
    }
  }

  // Rule 5: per-action input affordance requirements (touchwheel/param holds).
  return action_input_restriction_allows(action->type, trigger);
}

// Default variant for a consolidated family. Chosen so that "is this type
// valid for the trigger?" with no variant information returns true whenever
// ANY variant would work. For TEMPO and CONTROL, VARIANT_SET is the most
// permissive: it passes both the hold gate (not hold) and the fire-and-forget
// gate (SET is the one-shot variant of each family). The variant picker then
// filters precisely once the user opens it.
static action_variant_t default_variant_for_type(action_type_t type) {
  switch (type) {
    case ACTION_TEMPO:
    case ACTION_CONTROL:
    case ACTION_SCENE:
    case ACTION_PRESET:
      return VARIANT_SET;
    case ACTION_TRANSPORT:
      return VARIANT_PLAY;
    case ACTION_LFO:
      // MODIFY clears all four rules (not hold, not transport, fire-and-
      // forget, and exempt from the LFO ON_LOAD carve-out in Rule 4). Other
      // variants would falsely fail the type-level probe on ON_LOAD.
      return VARIANT_MODIFY;
    default:
      return VARIANT_NONE;
  }
}

// Thin wrapper for legacy by-type callers. Builds a synthetic action with
// the family's "least restrictive" default variant so the answer matches
// what the type picker should show.
bool action_is_valid_for_trigger(action_type_t type, action_trigger_type_t trigger) {
  action_t probe = {0};
  probe.type = type;
  probe.variant = default_variant_for_type(type);
  return action_is_valid_for_trigger_for(&probe, trigger);
}

bool action_variant_is_valid_for_trigger(action_type_t type,
                                         action_variant_t variant,
                                         action_trigger_type_t trigger) {
  action_t probe = {0};
  probe.type = type;
  probe.variant = variant;
  return action_is_valid_for_trigger_for(&probe, trigger);
}

// Returns true for actions that support timing options (non-HOLD actions)
// HOLD actions must execute immediately to preserve press/release pairing
// PUNCH_IN has built-in timing (always starts at next bar)
// ACTION_TEMPO supports timing at the type level (the menu lets the user
// pick a beat to fire on). Variant-specific exclusions (VARIANT_TAP,
// VARIANT_HOLD) are made by action_supports_timing_for() at the menu UI
// layer; this conservative by-type answer keeps existing call sites safe.
bool action_supports_timing(action_type_t type) {
  if (type == ACTION_NONE || type == ACTION_PUNCH_IN) return false;
  return !action_requires_hold(type);
}

// Preset/scene actions support timing but NOT repeat.
// ACTION_TEMPO is excluded at type level here, but variant-aware callers
// going through action_supports_repeat_for() get true for INCREMENT,
// DECREMENT, and CYCLE -- the variants whose repeat use case is musical
// (ramping tempo, stepping through presets in time).
bool action_supports_repeat(action_type_t type) {
  if (type == ACTION_NONE || action_requires_hold(type)) return false;
  switch (type) {
    case ACTION_PRESET:
    case ACTION_SCENE:
    case ACTION_TEMPO:
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

bool action_supports_timing_for(const action_t* action) {
  if (!action) return false;
  // Variant-precise carve-outs for consolidated families.
  if (action->type == ACTION_TEMPO) {
    // TAP is a mode interaction -- deferring it to a beat is nonsense.
    // HOLD needs a release pair, so it cannot be scheduled/repeated.
    if (action->variant == VARIANT_TAP || action->variant == VARIANT_HOLD) {
      return false;
    }
    return true;
  }
  if (action->type == ACTION_CONTROL) {
    // HOLD needs a release pair, so it cannot be scheduled/repeated.
    // SET and CYCLE schedule fine.
    if (action->variant == VARIANT_HOLD) return false;
    return true;
  }
  if (action->type == ACTION_PRESET) {
    // Same logic as CONTROL: HOLD needs a release pair, the rest schedule.
    // action_supports_timing(ACTION_PRESET) returns true at the type level
    // because PRESET is no longer in hold_actions[] (HOLD is now a variant);
    // intercept here so the menu doesn't show a Timing row for Preset Hold.
    if (action->variant == VARIANT_HOLD) return false;
    return true;
  }
  if (action->type == ACTION_TOUCHWHEEL) {
    // HOLD needs a release pair (can't be scheduled or repeated).
    // CYCLE is a press-only one-shot that schedules and repeats just fine.
    if (action->variant == VARIANT_HOLD) return false;
    return true;
  }
  if (action->type == ACTION_LFO) {
    // TOGGLE is interactive (the user expects it to flip on the press they
    // just made). START/STOP/MODIFY are all press-only one-shots that
    // schedule cleanly on a beat.
    if (action->variant == VARIANT_TOGGLE) return false;
    return true;
  }
  return action_supports_timing(action->type);
}

bool action_supports_repeat_for(const action_t* action) {
  if (!action) return false;
  if (action->type == ACTION_TEMPO) {
    switch (action->variant) {
      case VARIANT_INCREMENT:
      case VARIANT_DECREMENT:
      case VARIANT_CYCLE:
        return true;
      default:
        // TAP / SET / HOLD do not repeat.
        return false;
    }
  }
  if (action->type == ACTION_CONTROL) {
    // SET and CYCLE repeat (every press resends / advances).
    // HOLD does not repeat -- the release event has nowhere to go.
    return action->variant == VARIANT_SET || action->variant == VARIANT_CYCLE;
  }
  if (action->type == ACTION_PRESET) {
    // HOLD has no place to send repeated release events; suppress it.
    // Everything else in the family (SET, CYCLE, INC, DEC) repeats fine
    // (CYCLE advances the cursor, INC/DEC step the device preset, SET
    // just resends the same PC).
    return action->variant != VARIANT_HOLD;
  }
  if (action->type == ACTION_TOUCHWHEEL) {
    // HOLD has nowhere to send repeated release events; suppress.
    // CYCLE advances the mode cursor on every press -- repeats fine.
    return action->variant == VARIANT_CYCLE;
  }
  if (action->type == ACTION_LFO) {
    // Same TOGGLE carve-out as timing: starting/stopping/modifying on every
    // beat is musical; flipping on/off on every beat is just noise.
    return action->variant != VARIANT_TOGGLE;
  }
  return action_supports_repeat(action->type);
}

bool action_supports_transport_trigger(action_type_t type) {
  return action_supports_timing(type) && action_supports_repeat(type);
}

// Type-level morph support: conservative answer for consolidated families
// (true if ANY variant supports morph). Use action_supports_morph_for() when
// you have the full action_t to get the variant-precise answer.
bool action_supports_morph(action_type_t type) {
  switch (type) {
    case ACTION_CONTROL:
    case ACTION_RANDOMIZE:
    case ACTION_TEMPO:
      return true;
    default:
      return false;
  }
}

bool action_supports_morph_for(const action_t* action) {
  if (!action) return false;
  if (action->type == ACTION_CONTROL) {
    // SET sends immediately; HOLD and CYCLE feed the morph engine.
    return action->variant == VARIANT_HOLD || action->variant == VARIANT_CYCLE;
  }
  if (action->type == ACTION_TEMPO) {
    // Only HOLD glides between two BPMs; the other variants are one-shots
    // (TAP, SET) or already-quantized step changes (INCREMENT, DECREMENT,
    // CYCLE) where a ramp would feel mushy.
    return action->variant == VARIANT_HOLD;
  }
  return action_supports_morph(action->type);
}

bool action_supports_raise_flag(action_type_t type) {
  switch (type) {
    case ACTION_TRANSPORT:
    case ACTION_CONTROL:
    case ACTION_PRESET:
    case ACTION_TEMPO:
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

bool action_supports_raise_flag_for(const action_t* action) {
  if (!action) return false;
  if (!action_supports_raise_flag(action->type)) return false;
  // Any hold-shaped action -- HOLD variants of consolidated families
  // (TEMPO/CONTROL/PRESET), explicit *_HOLD singletons, and press/release
  // pairings like ACTION_NOTE that aren't named "Hold" but behave like one
  // -- defeats the flag-as-semaphore use case: the release event would
  // immediately unflag whatever the press flagged. action_requires_hold_for
  // already encodes this superset, so use it as the single source of truth.
  if (action_requires_hold_for(action)) return false;
  return true;
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
