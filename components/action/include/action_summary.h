#ifndef ACTION_SUMMARY_H
#define ACTION_SUMMARY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "action.h"
#include "continuous_mapping.h"
#include "scene.h"

// Summary structure for displaying action/mapping information
typedef struct {
  char input_name[24];    // Line 1: "Pad 3", "LFO 1", "Expression"
  char type_name[32];     // Line 2: "Control Change", "Notes", "Continuous"
  char param_name[32];    // Line 3: CC name, note range, LFO target
  char param_value[24];   // Line 4: discrete value or numeric
  bool has_param;         // Whether line 3 is populated
  bool has_value;         // Whether line 4 is populated
} action_summary_t;

// Input source types for setting input_name
typedef enum {
  SUMMARY_INPUT_PAD_0 = 0,
  SUMMARY_INPUT_PAD_1,
  SUMMARY_INPUT_PAD_2,
  SUMMARY_INPUT_PAD_3,
  SUMMARY_INPUT_PAD_4,
  SUMMARY_INPUT_PAD_5,
  SUMMARY_INPUT_PAD_6,
  SUMMARY_INPUT_PAD_7,
  SUMMARY_INPUT_PAD_8,
  SUMMARY_INPUT_PAD_9,
  SUMMARY_INPUT_PAD_10,
  SUMMARY_INPUT_PAD_11,
  SUMMARY_INPUT_TOUCHWHEEL,
  SUMMARY_INPUT_BUTTON_L,
  SUMMARY_INPUT_BUTTON_R,
  SUMMARY_INPUT_BUTTON_BOTH,
  SUMMARY_INPUT_BUMP,
  SUMMARY_INPUT_EXPRESSION,
  SUMMARY_INPUT_CV,
  SUMMARY_INPUT_PROXIMITY,
  SUMMARY_INPUT_ALS,
  SUMMARY_INPUT_LFO1,
  SUMMARY_INPUT_LFO2,
  SUMMARY_INPUT_SAMPLE_HOLD,
  SUMMARY_INPUT_ON_LOAD,
  SUMMARY_INPUT_ON_PLAY,
  SUMMARY_INPUT_UNKNOWN
} summary_input_t;

// Initialize an action_summary_t to empty state
void action_summary_init(action_summary_t *summary);

// Set the input name based on input source
// For pads 0-7, uses is_pads_mode to determine if showing "Pad N" or "Touchwheel"
void action_summary_set_input(action_summary_t *summary, summary_input_t input,
  bool is_pads_mode);

// Format action into summary (sets type_name, param_name, param_value)
// Requires scene_index to look up device for CC names
void action_format_summary(const action_t *action,
  action_summary_t *summary, uint8_t scene_index);

// Format continuous mapping (expression, CV, proximity, ALS, touchwheel continuous)
// Requires scene_index to look up device for CC names
void continuous_format_summary(const continuous_mapping_t *mapping,
  action_summary_t *summary, uint8_t scene_index);

// Format touchwheel mode summary (program change, tempo, etc.)
void touchwheel_format_summary(const scene_t *scene,
  action_summary_t *summary);

// Format LFO output summary
// lfo_slot: 0 or 1 (LFO1 or LFO2)
void lfo_format_summary(uint8_t lfo_slot, const scene_t *scene,
  action_summary_t *summary, uint8_t scene_index);

// Format Sample+Hold output summary
void sample_hold_format_summary(const scene_t *scene,
  action_summary_t *summary, uint8_t scene_index);

// Format the summary into a multi-line string with recolor codes
// Format: "#RRGGBB input_name#\ntype_name\nparam_name\nparam_value"
// buf: output buffer
// len: buffer length
// input_color: RGB color for input name line (e.g., 0x00FFFF for cyan)
void action_summary_format_display(const action_summary_t *summary,
  char *buf, size_t len, uint32_t input_color);

#endif // ACTION_SUMMARY_H
