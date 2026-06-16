#ifndef CV_MODE_MAPPING_H
#define CV_MODE_MAPPING_H

#include "scene.h"

// User-facing CV mode mapping
// Flattens cv_input_mode + cv.output_type into a single user-visible list so the
// device menu and web editor present one "Mode" selector.
typedef struct {
  const char* display_name;
  input_mode_t input_mode;
  output_type_t output_type;  // Only relevant when input_mode == INPUT_MODE_CV
  bool use_output_type;     // Whether to also set cv.output_type
} cv_mode_mapping_t;

#define NUM_CV_USER_MODES 9

// Order determines roller display order (must match the web CV_USER_MODES list)
static const cv_mode_mapping_t g_cv_mode_mappings[] = {
  { "Disabled",       INPUT_MODE_NONE,   OUTPUT_TYPE_CC,          false },
  { "Control Change", INPUT_MODE_CV,     OUTPUT_TYPE_CC,          true  },
  { "CV/Gate",        INPUT_MODE_NOTE,   OUTPUT_TYPE_CC,          false },
  { "Audio",          INPUT_MODE_AUDIO,  OUTPUT_TYPE_CC,          false },
  { "Trigger",        INPUT_MODE_TRIGGER, OUTPUT_TYPE_CC,         false },
  { "Notes",          INPUT_MODE_CV,     OUTPUT_TYPE_NOTE,        true  },
  { "LFO Rate",       INPUT_MODE_CV,     OUTPUT_TYPE_LFO_RATE,    true  },
  { "LFO Depth",      INPUT_MODE_CV,     OUTPUT_TYPE_LFO_DEPTH,   true  },
  { "Tempo Nudge",    INPUT_MODE_CV,     OUTPUT_TYPE_TEMPO_NUDGE, true  },
};

static inline const cv_mode_mapping_t* cv_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_CV_USER_MODES) return NULL;
  return &g_cv_mode_mappings[user_mode_index];
}

static inline const char* cv_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_CV_USER_MODES) return "Unknown";
  return g_cv_mode_mappings[user_mode_index].display_name;
}

// Resolve the current scene settings to a user-facing mode index.
// Returns 0 (Disabled) if nothing matches.
static inline uint8_t cv_get_current_mode_index(const scene_t* scene) {
  if (!scene) return 0;

  input_mode_t mode = scene->cv_input_mode;
  output_type_t output = scene->cv.output_type;

  for (uint8_t i = 0; i < NUM_CV_USER_MODES; i++) {
    if (g_cv_mode_mappings[i].input_mode != mode) continue;
    if (mode == INPUT_MODE_CV) {
      if (g_cv_mode_mappings[i].use_output_type &&
          g_cv_mode_mappings[i].output_type == output) {
        return i;
      }
    } else {
      return i;
    }
  }
  return 0;  // Default to Disabled
}

#endif // CV_MODE_MAPPING_H
