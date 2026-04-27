#include "action_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Action type names (for debugging/console)
// ============================================================================
static const char* action_type_names[] = {
  [ACTION_NONE] = "None",
  [ACTION_PRESET_INC] = "Preset +1",
  [ACTION_PRESET_DEC] = "Preset -1",
  [ACTION_PRESET] = "Set Preset",
  [ACTION_PRESET_HOLD] = "Preset Hold",
  [ACTION_PRESET_CYCLE] = "Preset Cycle",
  [ACTION_SCENE_INC] = "Scene +1",
  [ACTION_SCENE_DEC] = "Scene -1",
  [ACTION_SCENE] = "Set Scene",
  [ACTION_PLAY] = "Play",
  [ACTION_STOP] = "Stop",
  [ACTION_PAUSE] = "Pause",
  [ACTION_RECORD] = "Record",
  [ACTION_TAP] = "Tap",
  [ACTION_TAP_TEMPO] = "Tap Tempo",
  [ACTION_SET_TEMPO] = "Set Tempo",
  [ACTION_TEMPO_INC] = "Tempo +1",
  [ACTION_TEMPO_DEC] = "Tempo -1",
  [ACTION_TEMPO_HOLD] = "Tempo Hold",
  [ACTION_TEMPO_CYCLE] = "Tempo Cycle",
  [ACTION_CONTROL_CHANGE] = "Control Change",
  [ACTION_CONTROL_HOLD] = "Control Hold",
  [ACTION_CONTROL_CYCLE] = "Control Cycle",
  [ACTION_NOTE] = "Note",
  [ACTION_RANDOMIZE] = "Randomize",
  [ACTION_CONFIRM_PENDING] = "Confirm Pending",
  [ACTION_RESET] = "Reset",
  [ACTION_SUSTAIN] = "Sustain",
  [ACTION_SOSTENUTO] = "Sostenuto",
  [ACTION_TOUCHWHEEL_HOLD] = "Touchwheel Hold",
  [ACTION_TOUCHWHEEL_CYCLE] = "Touchwheel Cycle",
  [ACTION_LFO_START] = "LFO Start",
  [ACTION_LFO_STOP] = "LFO Stop",
  [ACTION_LFO_TOGGLE] = "LFO Toggle",
  [ACTION_LFO_SHAPE] = "LFO Shape",
  [ACTION_CLOCK_TOGGLE] = "Clock Toggle",
  [ACTION_CLOCK_HOLD] = "Clock Hold",
  [ACTION_CLOCK_BURST] = "Clock Burst",
  [ACTION_CUT_TOGGLE] = "Cut Toggle",
  [ACTION_CUT_HOLD] = "Cut Hold",
  [ACTION_SET_UI] = "Set UI",
  [ACTION_UI_HOLD] = "UI Hold",
  [ACTION_UI_CYCLE] = "UI Cycle",
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
