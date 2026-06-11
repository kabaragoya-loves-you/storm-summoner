#ifndef EXPRESSION_MODE_MAPPING_H
#define EXPRESSION_MODE_MAPPING_H

#include "scene.h"

// User-facing expression mode mapping
// Flattens expression_mode + output_type into a single user-visible list so the
// device menu and web editor present one "Mode" selector.
typedef struct {
  const char* display_name;
  expression_mode_t mode;
  output_type_t output_type;  // Only relevant when mode == EXPRESSION_MODE_PEDAL
  bool use_output_type;       // Whether to also set output_type
} expression_mode_mapping_t;

#define NUM_EXPRESSION_USER_MODES 9

// Order determines roller display order (must match the web EXPRESSION_USER_MODES list)
static const expression_mode_mapping_t g_expression_mode_mappings[] = {
  { "Disabled",       EXPRESSION_MODE_NONE,      OUTPUT_TYPE_CC,          false },
  { "Control Change", EXPRESSION_MODE_PEDAL,     OUTPUT_TYPE_CC,          true  },
  { "Sustain",        EXPRESSION_MODE_SUSTAIN,   OUTPUT_TYPE_CC,          false },
  { "Sostenuto",      EXPRESSION_MODE_SOSTENUTO, OUTPUT_TYPE_CC,          false },
  { "Switch",         EXPRESSION_MODE_SWITCH,    OUTPUT_TYPE_CC,          false },
  { "LFO Rate",       EXPRESSION_MODE_PEDAL,     OUTPUT_TYPE_LFO_RATE,    true  },
  { "LFO Depth",      EXPRESSION_MODE_PEDAL,     OUTPUT_TYPE_LFO_DEPTH,   true  },
  { "Notes",          EXPRESSION_MODE_PEDAL,     OUTPUT_TYPE_NOTE,        true  },
  { "Tempo Nudge",    EXPRESSION_MODE_PEDAL,     OUTPUT_TYPE_TEMPO_NUDGE, true  },
};

static inline const expression_mode_mapping_t* expression_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_EXPRESSION_USER_MODES) return NULL;
  return &g_expression_mode_mappings[user_mode_index];
}

static inline const char* expression_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_EXPRESSION_USER_MODES) return "Unknown";
  return g_expression_mode_mappings[user_mode_index].display_name;
}

// Resolve the current scene settings to a user-facing mode index.
// Returns 0 (Disabled) if nothing matches.
static inline uint8_t expression_get_current_mode_index(const scene_t* scene) {
  if (!scene) return 0;

  expression_mode_t mode = scene->expression_mode;
  output_type_t output = scene->expression.output_type;

  for (uint8_t i = 0; i < NUM_EXPRESSION_USER_MODES; i++) {
    if (g_expression_mode_mappings[i].mode != mode) continue;
    if (mode == EXPRESSION_MODE_PEDAL) {
      if (g_expression_mode_mappings[i].use_output_type &&
          g_expression_mode_mappings[i].output_type == output) {
        return i;
      }
    } else {
      return i;
    }
  }
  return 0;  // Default to Disabled
}

#endif // EXPRESSION_MODE_MAPPING_H
