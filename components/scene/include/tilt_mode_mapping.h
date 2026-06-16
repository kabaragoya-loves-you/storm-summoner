#ifndef TILT_MODE_MAPPING_H
#define TILT_MODE_MAPPING_H

#include "scene.h"

typedef struct {
  const char* display_name;
  output_type_t output_type;
  bool enabled;
} tilt_mode_mapping_t;

#define NUM_TILT_USER_MODES 7

static const tilt_mode_mapping_t g_tilt_mode_mappings[] = {
  { "Disabled",       OUTPUT_TYPE_CC,          false },
  { "Control Change", OUTPUT_TYPE_CC,          true  },
  { "Notes",          OUTPUT_TYPE_NOTE,        true  },
  { "LFO Rate",       OUTPUT_TYPE_LFO_RATE,    true  },
  { "LFO Depth",      OUTPUT_TYPE_LFO_DEPTH,   true  },
  { "Pitch Bend",     OUTPUT_TYPE_PITCH_BEND,  true  },
  { "Tempo Nudge",    OUTPUT_TYPE_TEMPO_NUDGE, true  },
};

static inline const tilt_mode_mapping_t* tilt_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_TILT_USER_MODES) return NULL;
  return &g_tilt_mode_mappings[user_mode_index];
}

static inline const char* tilt_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_TILT_USER_MODES) return "Unknown";
  return g_tilt_mode_mappings[user_mode_index].display_name;
}

static inline uint8_t tilt_get_current_mode_index(const continuous_mapping_t* mapping) {
  if (!mapping || !mapping->enabled) return 0;

  output_type_t output = mapping->output_type;
  for (uint8_t i = 1; i < NUM_TILT_USER_MODES; i++) {
    if (g_tilt_mode_mappings[i].output_type == output) return i;
  }
  return 1;
}

#endif // TILT_MODE_MAPPING_H
