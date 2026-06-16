#ifndef TOUCHWHEEL_MODE_MAPPING_H
#define TOUCHWHEEL_MODE_MAPPING_H

#include "scene.h"

// User-facing touchwheel mode mapping
// Maps user-visible mode names to internal mode + output_type combinations
typedef struct {
  const char* display_name;
  touchwheel_mode_t mode;
  output_type_t output_type;      // Only relevant for CONTINUOUS mode
  bool use_output_type;           // Whether to also set output_type
  touchwheel_style_t default_style;
  bool supports_style_selection;  // User can change style
} touchwheel_mode_mapping_t;

// Number of user-facing touchwheel modes
#define NUM_TOUCHWHEEL_USER_MODES 13

// User-facing mode mappings - order determines roller display order
// Index 0-12 maps to these modes
static const touchwheel_mode_mapping_t g_touchwheel_mode_mappings[] = {
  { "Pads",           TOUCHWHEEL_MODE_PADS,           OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, false },
  { "Control Change", TOUCHWHEEL_MODE_CONTINUOUS,     OUTPUT_TYPE_CC,          true,  TOUCHWHEEL_STYLE_ENDLESS,  true  },
  { "Program Change", TOUCHWHEEL_MODE_PROGRAM_CHANGE, OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ENDLESS,  false },
  { "Tempo",          TOUCHWHEEL_MODE_SET_TEMPO,      OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ENDLESS,  true  },
  { "Pitch Bend",     TOUCHWHEEL_MODE_PITCH_BEND,     OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_BIPOLAR,  false },
  { "After Touch",    TOUCHWHEEL_MODE_AFTERTOUCH,     OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, true  },
  { "Notes",          TOUCHWHEEL_MODE_CONTINUOUS,     OUTPUT_TYPE_NOTE,        true,  TOUCHWHEEL_STYLE_ODOMETER, true  },
  { "Double CC",      TOUCHWHEEL_MODE_DOUBLE_CC,      OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ENDLESS,  true  },
  { "Velocity",       TOUCHWHEEL_MODE_VELOCITY,       OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, false },
  { "LFO Rate",       TOUCHWHEEL_MODE_LFO_RATE,       OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, true  },
  { "LFO Depth",      TOUCHWHEEL_MODE_LFO_DEPTH,      OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, true  },
  { "RTG Rate",       TOUCHWHEEL_MODE_RTG_RATE,       OUTPUT_TYPE_CC,          false, TOUCHWHEEL_STYLE_ODOMETER, true  },
  { "Tempo Nudge",    TOUCHWHEEL_MODE_CONTINUOUS,     OUTPUT_TYPE_TEMPO_NUDGE, true,  TOUCHWHEEL_STYLE_ODOMETER, true  },
};

// Get mode mapping by user-facing index (0-7)
// Returns NULL if index is out of range
static inline const touchwheel_mode_mapping_t* touchwheel_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_TOUCHWHEEL_USER_MODES) return NULL;
  return &g_touchwheel_mode_mappings[user_mode_index];
}

// Get display name for a user-facing mode index
static inline const char* touchwheel_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_TOUCHWHEEL_USER_MODES) return "Unknown";
  return g_touchwheel_mode_mappings[user_mode_index].display_name;
}

// Find user-facing mode index from current scene settings
// Returns 0 if not found (defaults to Pads)
static inline uint8_t touchwheel_get_current_mode_index(scene_t* scene) {
  if (!scene) return 0;
  
  touchwheel_mode_t mode = scene_get_effective_touchwheel_mode(scene);
  output_type_t output = scene->touchwheel.output_type;
  
  for (uint8_t i = 0; i < NUM_TOUCHWHEEL_USER_MODES; i++) {
    if (g_touchwheel_mode_mappings[i].mode == mode) {
      // For CONTINUOUS mode, also check output type
      if (mode == TOUCHWHEEL_MODE_CONTINUOUS) {
        if (g_touchwheel_mode_mappings[i].use_output_type &&
            g_touchwheel_mode_mappings[i].output_type == output) {
          return i;
        }
      } else {
        return i;
      }
    }
  }
  return 0;  // Default to Pads
}

#endif // TOUCHWHEEL_MODE_MAPPING_H
