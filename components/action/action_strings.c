#include "action_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Action type names (for debugging/console)
// ============================================================================
static const char* action_type_names[] = {
  [ACTION_NONE] = "None",
  [ACTION_PRESET] = "Preset",
  [ACTION_SCENE] = "Scene",
  [ACTION_TRANSPORT] = "Transport",
  [ACTION_TEMPO] = "Tempo",
  [ACTION_CONTROL] = "Control",
  [ACTION_NOTE] = "Note",
  [ACTION_RANDOMIZE] = "Randomize",
  [ACTION_CONFIRM_PENDING] = "Confirm Pending",
  [ACTION_RESET] = "Reset",
  [ACTION_PIANO_PEDAL] = "Piano Pedal",
  [ACTION_TOUCHWHEEL] = "Touchwheel",
  [ACTION_LFO] = "LFO",
  [ACTION_CLOCK] = "Clock",
  [ACTION_CUT] = "Cut",
  [ACTION_UI] = "UI",
  [ACTION_PARAM_HOLD] = "Param Hold",
  [ACTION_PARAM_CYCLE] = "Param Cycle",
  [ACTION_RTG_TOGGLE] = "RTG Toggle",
  [ACTION_RTG_HOLD] = "RTG Hold",
  [ACTION_SAMPLE_HOLD_TOGGLE] = "S+H Toggle",
  [ACTION_SAMPLE_HOLD_HOLD] = "S+H Hold",
  [ACTION_STEP] = "Step",
  [ACTION_PUNCH_IN] = "Punch-In",
  [ACTION_FLAG_CEREMONY] = "Flag Ceremony",
  [ACTION_BOOMERANG] = "Boomerang"
};

const char* action_type_to_string(action_type_t type) {
  if (type >= ACTION_MAX) return "Unknown";
  const char* name = action_type_names[type];
  return name ? name : "Unknown";
}

const char* action_type_name(action_type_t type) {
  return action_type_to_string(type);
}

// ============================================================================
// Action variant names (for consolidated families)
// ============================================================================

static const char* action_variant_display_names[] = {
  [VARIANT_NONE]      = "",
  [VARIANT_INCREMENT] = "Increment",
  [VARIANT_DECREMENT] = "Decrement",
  [VARIANT_SET]       = "Set",
  [VARIANT_HOLD]      = "Hold",
  [VARIANT_CYCLE]     = "Cycle",
  [VARIANT_TOGGLE]    = "Toggle",
  [VARIANT_START]     = "Start",
  [VARIANT_STOP]      = "Stop",
  [VARIANT_TAP]       = "Tap",
  [VARIANT_BURST]     = "Burst",
  [VARIANT_PLAY]      = "Play",
  [VARIANT_PAUSE]     = "Pause",
  [VARIANT_RECORD]    = "Record",
  [VARIANT_MODIFY]    = "Modify",
};

const char* action_variant_to_string(action_variant_t variant) {
  if (variant >= VARIANT_MAX) return "";
  const char* name = action_variant_display_names[variant];
  return name ? name : "";
}

bool action_type_has_variants(action_type_t type) {
  // Add new consolidated families here as they are introduced. Singletons
  // (NOTE, BOOMERANG, etc.) and types with a single behavior return false
  // so action_get_display_name does not render a trailing "> " separator.
  switch (type) {
    case ACTION_TEMPO:
    case ACTION_CONTROL:
    case ACTION_SCENE:
    case ACTION_PRESET:
    case ACTION_TRANSPORT:
    case ACTION_TOUCHWHEEL:
    case ACTION_LFO:
    case ACTION_CLOCK:
      return true;
    case ACTION_CUT:
      return true;
    case ACTION_UI:
      return true;
    default:
      return false;
  }
}

// Compact per-(type, variant) display names for consolidated families.
// These are intentionally kept short (<= 14 chars) so they fit the 240x240px
// circular display's ~12-14 char working width in list labels like
// "Load Action 1\n<name>" and "Left\n<name>".
//
// The standalone action_type_to_string() label ("Tempo") is reserved for the
// type-picker roller, where the user is choosing the family. The composite
// form below is for everywhere the user sees an already-configured action.
static const char* tempo_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_TAP:       return "Tap Tempo";
    case VARIANT_SET:       return "Set Tempo";
    case VARIANT_INCREMENT: return "Tempo +1";
    case VARIANT_DECREMENT: return "Tempo -1";
    case VARIANT_HOLD:      return "Tempo Hold";
    case VARIANT_CYCLE:     return "Tempo Cycle";
    default:                return "Tempo";
  }
}

// "Control Change" is the well-known MIDI term, kept intact for SET.
static const char* control_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_SET:   return "Control Change";
    case VARIANT_HOLD:  return "Control Hold";
    case VARIANT_CYCLE: return "Control Cycle";
    default:            return "Control";
  }
}

// Scene family: "Set Scene" keeps the historical label; INC/DEC compress
// well as "Scene +1" / "Scene -1" for the ~12-14 char display.
static const char* scene_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_SET:       return "Set Scene";
    case VARIANT_INCREMENT: return "Scene +1";
    case VARIANT_DECREMENT: return "Scene -1";
    default:                return "Scene";
  }
}

// Preset family: same shape as Scene -- "Set Preset" is the historical
// label, HOLD/CYCLE keep their pre-consolidation names, INC/DEC compress.
static const char* preset_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_SET:       return "Set Preset";
    case VARIANT_HOLD:      return "Preset Hold";
    case VARIANT_CYCLE:     return "Preset Cycle";
    case VARIANT_INCREMENT: return "Preset +1";
    case VARIANT_DECREMENT: return "Preset -1";
    default:                return "Preset";
  }
}

// Transport family: operation-as-variant -- bare verbs unambiguously name the
// operation and a "Transport" prefix would only waste pixels on the ~12-14
// char display.
static const char* transport_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_PLAY:   return "Play";
    case VARIANT_STOP:   return "Stop";
    case VARIANT_PAUSE:  return "Pause";
    case VARIANT_RECORD: return "Record";
    default:             return "Transport";
  }
}

// Touchwheel family: pre-consolidation labels were "Touchwheel Hold" /
// "Touchwheel Cycle"; keep that wording so user-facing pads/buttons lists
// and Khyron readouts read the same as before the rename.
static const char* touchwheel_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_HOLD:  return "Touchwheel Hold";
    case VARIANT_CYCLE: return "Touchwheel Cycle";
    default:            return "Touchwheel";
  }
}

// LFO family: SHAPE collapsed into MODIFY (general parameter override). All
// three of START/STOP/TOGGLE keep their pre-consolidation wording.
static const char* lfo_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_START:  return "LFO Start";
    case VARIANT_STOP:   return "LFO Stop";
    case VARIANT_TOGGLE: return "LFO Toggle";
    case VARIANT_MODIFY: return "LFO Modify";
    default:             return "LFO";
  }
}

static const char* clock_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_TOGGLE: return "Clock Toggle";
    case VARIANT_HOLD:   return "Clock Hold";
    case VARIANT_BURST:  return "Clock Burst";
    default:             return "Clock";
  }
}

static const char* cut_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_TOGGLE: return "Cut Toggle";
    case VARIANT_HOLD:   return "Cut Hold";
    default:             return "Cut";
  }
}

static const char* ui_variant_display(action_variant_t v) {
  switch (v) {
    case VARIANT_SET:   return "Set UI";
    case VARIANT_HOLD:  return "UI Hold";
    case VARIANT_CYCLE: return "UI Cycle";
    default:            return "UI";
  }
}

void action_get_display_name(const action_t* action, char* buf, size_t len) {
  if (!buf || len == 0) return;
  if (!action) {
    buf[0] = '\0';
    return;
  }
  if (action->type == ACTION_TEMPO) {
    snprintf(buf, len, "%s", tempo_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_CONTROL) {
    snprintf(buf, len, "%s", control_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_SCENE) {
    snprintf(buf, len, "%s", scene_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_PRESET) {
    snprintf(buf, len, "%s", preset_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_TRANSPORT) {
    snprintf(buf, len, "%s", transport_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_TOUCHWHEEL) {
    snprintf(buf, len, "%s", touchwheel_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_LFO) {
    snprintf(buf, len, "%s", lfo_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_CLOCK) {
    snprintf(buf, len, "%s", clock_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_CUT) {
    snprintf(buf, len, "%s", cut_variant_display(action->variant));
    return;
  }
  if (action->type == ACTION_UI) {
    snprintf(buf, len, "%s", ui_variant_display(action->variant));
    return;
  }
  snprintf(buf, len, "%s", action_type_to_string(action->type));
}

// ============================================================================
// Action timing string conversion
// ============================================================================

static char s_timing_str[16];

const char* action_timing_to_string(action_timing_t timing, uint8_t beat) {
  switch (timing) {
    case ACTION_TIMING_IMMEDIATE:
      return "immediate";
    case ACTION_TIMING_NEXT_BEAT:
      return "beat";
    case ACTION_TIMING_SPECIFIC_BEAT:
      snprintf(s_timing_str, sizeof(s_timing_str), "beat_%d", beat);
      return s_timing_str;
    default:
      return "immediate";
  }
}

void action_timing_from_string(const char* str, action_timing_t* timing, uint8_t* beat) {
  *timing = ACTION_TIMING_IMMEDIATE;
  *beat = 1;

  if (!str || str[0] == '\0') return;

  if (strcmp(str, "immediate") == 0) {
    *timing = ACTION_TIMING_IMMEDIATE;
  } else if (strcmp(str, "beat") == 0) {
    *timing = ACTION_TIMING_NEXT_BEAT;
  } else if (strncmp(str, "beat_", 5) == 0) {
    int beat_num = atoi(str + 5);
    if (beat_num >= 1 && beat_num <= 16) {
      *timing = ACTION_TIMING_SPECIFIC_BEAT;
      *beat = (uint8_t)beat_num;
    } else {
      *timing = ACTION_TIMING_SPECIFIC_BEAT;
      *beat = 1;
    }
  }
}

// ============================================================================
// Repeat division string conversion
// ============================================================================

const char* action_repeat_division_to_string(action_repeat_division_t div) {
  switch (div) {
    case ACTION_REPEAT_16_BARS:   return "16_bars";
    case ACTION_REPEAT_12_BARS:   return "12_bars";
    case ACTION_REPEAT_8_BARS:    return "8_bars";
    case ACTION_REPEAT_4_BARS:    return "4_bars";
    case ACTION_REPEAT_2_BARS:    return "2_bars";
    case ACTION_REPEAT_1_BAR:     return "1_bar";
    case ACTION_REPEAT_HALF:      return "half";
    case ACTION_REPEAT_QUARTER:   return "quarter";
    case ACTION_REPEAT_EIGHTH:    return "eighth";
    case ACTION_REPEAT_SIXTEENTH: return "sixteenth";
    case ACTION_REPEAT_32ND:      return "32nd";
    default: return "quarter";
  }
}

action_repeat_division_t action_repeat_division_from_string(const char* str) {
  if (!str) return ACTION_REPEAT_QUARTER;
  if (strcmp(str, "16_bars") == 0) return ACTION_REPEAT_16_BARS;
  if (strcmp(str, "12_bars") == 0) return ACTION_REPEAT_12_BARS;
  if (strcmp(str, "8_bars") == 0) return ACTION_REPEAT_8_BARS;
  if (strcmp(str, "4_bars") == 0) return ACTION_REPEAT_4_BARS;
  if (strcmp(str, "2_bars") == 0) return ACTION_REPEAT_2_BARS;
  if (strcmp(str, "1_bar") == 0) return ACTION_REPEAT_1_BAR;
  if (strcmp(str, "half") == 0) return ACTION_REPEAT_HALF;
  if (strcmp(str, "quarter") == 0) return ACTION_REPEAT_QUARTER;
  if (strcmp(str, "eighth") == 0) return ACTION_REPEAT_EIGHTH;
  if (strcmp(str, "sixteenth") == 0) return ACTION_REPEAT_SIXTEENTH;
  if (strcmp(str, "32nd") == 0) return ACTION_REPEAT_32ND;
  return ACTION_REPEAT_QUARTER;
}

// For divisions >= 1 bar, returns beats_per_bar * bars
// For divisions < 1 bar, returns 0 (will use sub-beat timing)
uint8_t action_repeat_division_to_beats(action_repeat_division_t div, uint8_t beats_per_bar) {
  if (beats_per_bar == 0) beats_per_bar = 4;

  switch (div) {
    case ACTION_REPEAT_16_BARS:   return beats_per_bar * 16;
    case ACTION_REPEAT_12_BARS:   return beats_per_bar * 12;
    case ACTION_REPEAT_8_BARS:    return beats_per_bar * 8;
    case ACTION_REPEAT_4_BARS:    return beats_per_bar * 4;
    case ACTION_REPEAT_2_BARS:    return beats_per_bar * 2;
    case ACTION_REPEAT_1_BAR:     return beats_per_bar;
    case ACTION_REPEAT_HALF:      return 2;
    case ACTION_REPEAT_QUARTER:   return 1;
    case ACTION_REPEAT_EIGHTH:    return 0;
    case ACTION_REPEAT_SIXTEENTH: return 0;
    case ACTION_REPEAT_32ND:      return 0;
    default: return 1;
  }
}

// ============================================================================
// Punch-in duration string conversion
// ============================================================================

const char* punch_in_duration_to_string(punch_in_duration_t duration) {
  switch (duration) {
    case PUNCH_IN_1_BEAT:  return "1_beat";
    case PUNCH_IN_2_BEATS: return "2_beats";
    case PUNCH_IN_3_BEATS: return "3_beats";
    case PUNCH_IN_4_BEATS: return "4_beats";
    case PUNCH_IN_5_BEATS: return "5_beats";
    case PUNCH_IN_6_BEATS: return "6_beats";
    case PUNCH_IN_7_BEATS: return "7_beats";
    case PUNCH_IN_1_BAR:   return "1_bar";
    case PUNCH_IN_2_BARS:  return "2_bars";
    case PUNCH_IN_4_BARS:  return "4_bars";
    case PUNCH_IN_8_BARS:  return "8_bars";
    case PUNCH_IN_16_BARS: return "16_bars";
    default: return "1_bar";
  }
}

const char* punch_in_duration_to_display_string(punch_in_duration_t duration) {
  switch (duration) {
    case PUNCH_IN_1_BEAT:  return "1 Beat";
    case PUNCH_IN_2_BEATS: return "2 Beats";
    case PUNCH_IN_3_BEATS: return "3 Beats";
    case PUNCH_IN_4_BEATS: return "4 Beats";
    case PUNCH_IN_5_BEATS: return "5 Beats";
    case PUNCH_IN_6_BEATS: return "6 Beats";
    case PUNCH_IN_7_BEATS: return "7 Beats";
    case PUNCH_IN_1_BAR:   return "1 Bar";
    case PUNCH_IN_2_BARS:  return "2 Bars";
    case PUNCH_IN_4_BARS:  return "4 Bars";
    case PUNCH_IN_8_BARS:  return "8 Bars";
    case PUNCH_IN_16_BARS: return "16 Bars";
    default: return "1 Bar";
  }
}

punch_in_duration_t punch_in_duration_from_string(const char* str) {
  if (!str) return PUNCH_IN_1_BAR;
  if (strcmp(str, "1_beat") == 0) return PUNCH_IN_1_BEAT;
  if (strcmp(str, "2_beats") == 0) return PUNCH_IN_2_BEATS;
  if (strcmp(str, "3_beats") == 0) return PUNCH_IN_3_BEATS;
  if (strcmp(str, "4_beats") == 0) return PUNCH_IN_4_BEATS;
  if (strcmp(str, "5_beats") == 0) return PUNCH_IN_5_BEATS;
  if (strcmp(str, "6_beats") == 0) return PUNCH_IN_6_BEATS;
  if (strcmp(str, "7_beats") == 0) return PUNCH_IN_7_BEATS;
  if (strcmp(str, "1_bar") == 0) return PUNCH_IN_1_BAR;
  if (strcmp(str, "2_bars") == 0) return PUNCH_IN_2_BARS;
  if (strcmp(str, "4_bars") == 0) return PUNCH_IN_4_BARS;
  if (strcmp(str, "8_bars") == 0) return PUNCH_IN_8_BARS;
  if (strcmp(str, "16_bars") == 0) return PUNCH_IN_16_BARS;
  return PUNCH_IN_1_BAR;
}

uint8_t punch_in_duration_to_beats(punch_in_duration_t duration, uint8_t beats_per_bar) {
  if (beats_per_bar == 0) beats_per_bar = 4;

  switch (duration) {
    case PUNCH_IN_1_BEAT:  return 1;
    case PUNCH_IN_2_BEATS: return 2;
    case PUNCH_IN_3_BEATS: return 3;
    case PUNCH_IN_4_BEATS: return 4;
    case PUNCH_IN_5_BEATS: return 5;
    case PUNCH_IN_6_BEATS: return 6;
    case PUNCH_IN_7_BEATS: return 7;
    case PUNCH_IN_1_BAR:   return beats_per_bar;
    case PUNCH_IN_2_BARS:  return beats_per_bar * 2;
    case PUNCH_IN_4_BARS:  return beats_per_bar * 4;
    case PUNCH_IN_8_BARS:  return beats_per_bar * 8;
    case PUNCH_IN_16_BARS: return beats_per_bar * 16;
    default: return beats_per_bar;
  }
}

// ============================================================================
// Morph string conversion
// ============================================================================

const char* morph_steps_mode_to_string(morph_steps_mode_t mode) {
  switch (mode) {
    case MORPH_STEPS_AUTO:   return "auto";
    case MORPH_STEPS_COARSE: return "coarse";
    case MORPH_STEPS_MEDIUM: return "medium";
    case MORPH_STEPS_FINE:   return "fine";
    case MORPH_STEPS_MANUAL: return "manual";
    default: return "auto";
  }
}

morph_steps_mode_t morph_steps_mode_from_string(const char* str) {
  if (!str) return MORPH_STEPS_AUTO;
  if (strcmp(str, "auto") == 0) return MORPH_STEPS_AUTO;
  if (strcmp(str, "coarse") == 0) return MORPH_STEPS_COARSE;
  if (strcmp(str, "medium") == 0) return MORPH_STEPS_MEDIUM;
  if (strcmp(str, "fine") == 0) return MORPH_STEPS_FINE;
  if (strcmp(str, "manual") == 0) return MORPH_STEPS_MANUAL;
  return MORPH_STEPS_AUTO;
}

const char* morph_timing_mode_to_string(morph_timing_mode_t mode) {
  switch (mode) {
    case MORPH_TIMING_FEEL:     return "feel";
    case MORPH_TIMING_DURATION: return "duration";
    case MORPH_TIMING_SYNC:     return "sync";
    default: return "feel";
  }
}

morph_timing_mode_t morph_timing_mode_from_string(const char* str) {
  if (!str) return MORPH_TIMING_FEEL;
  if (strcmp(str, "feel") == 0) return MORPH_TIMING_FEEL;
  if (strcmp(str, "duration") == 0) return MORPH_TIMING_DURATION;
  if (strcmp(str, "sync") == 0) return MORPH_TIMING_SYNC;
  return MORPH_TIMING_FEEL;
}

const char* morph_feel_to_string(morph_feel_t feel) {
  switch (feel) {
    case MORPH_FEEL_FAST:   return "fast";
    case MORPH_FEEL_MEDIUM: return "medium";
    case MORPH_FEEL_SLOW:   return "slow";
    default: return "medium";
  }
}

morph_feel_t morph_feel_from_string(const char* str) {
  if (!str) return MORPH_FEEL_MEDIUM;
  if (strcmp(str, "fast") == 0) return MORPH_FEEL_FAST;
  if (strcmp(str, "medium") == 0) return MORPH_FEEL_MEDIUM;
  if (strcmp(str, "slow") == 0) return MORPH_FEEL_SLOW;
  return MORPH_FEEL_MEDIUM;
}

const char* morph_division_to_string(morph_division_t div) {
  switch (div) {
    case MORPH_DIV_1_BEAT:  return "1_beat";
    case MORPH_DIV_1_BAR:   return "1_bar";
    case MORPH_DIV_2_BARS:  return "2_bars";
    case MORPH_DIV_4_BARS:  return "4_bars";
    case MORPH_DIV_BEAT_2:  return "beat_2";
    case MORPH_DIV_BEAT_3:  return "beat_3";
    case MORPH_DIV_BEAT_4:  return "beat_4";
    case MORPH_DIV_2_BEATS: return "2_beats";
    case MORPH_DIV_3_BEATS: return "3_beats";
    case MORPH_DIV_3_BARS:  return "3_bars";
    default: return "1_bar";
  }
}

morph_division_t morph_division_from_string(const char* str) {
  if (!str) return MORPH_DIV_1_BAR;
  // New format
  if (strcmp(str, "1_beat") == 0) return MORPH_DIV_1_BEAT;
  if (strcmp(str, "1_bar") == 0) return MORPH_DIV_1_BAR;
  if (strcmp(str, "2_beats") == 0) return MORPH_DIV_2_BEATS;
  if (strcmp(str, "3_beats") == 0) return MORPH_DIV_3_BEATS;
  if (strcmp(str, "3_bars") == 0) return MORPH_DIV_3_BARS;
  // Legacy format (backward compatibility)
  if (strcmp(str, "beat") == 0) return MORPH_DIV_1_BEAT;
  if (strcmp(str, "bar") == 0) return MORPH_DIV_1_BAR;
  if (strcmp(str, "2_bars") == 0) return MORPH_DIV_2_BARS;
  if (strcmp(str, "4_bars") == 0) return MORPH_DIV_4_BARS;
  if (strcmp(str, "beat_2") == 0) return MORPH_DIV_BEAT_2;
  if (strcmp(str, "beat_3") == 0) return MORPH_DIV_BEAT_3;
  if (strcmp(str, "beat_4") == 0) return MORPH_DIV_BEAT_4;
  return MORPH_DIV_1_BAR;
}
