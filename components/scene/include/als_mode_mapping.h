#ifndef ALS_MODE_MAPPING_H
#define ALS_MODE_MAPPING_H

#include "scene.h"

// User-facing ALS mode mapping
// Flattens als.enabled + output_type into a single user-visible list so the
// device menu and web editor present one "Mode" selector.
typedef struct {
  const char* display_name;
  output_type_t output_type;
  bool enabled;
} als_mode_mapping_t;

#define NUM_ALS_USER_MODES 6

// Order determines roller display order (must match the web ALS_USER_MODES list)
static const als_mode_mapping_t g_als_mode_mappings[] = {
  { "Disabled",       OUTPUT_TYPE_CC,          false },
  { "Control Change", OUTPUT_TYPE_CC,          true  },
  { "Notes",          OUTPUT_TYPE_NOTE,        true  },
  { "LFO Rate",       OUTPUT_TYPE_LFO_RATE,    true  },
  { "LFO Depth",      OUTPUT_TYPE_LFO_DEPTH,   true  },
  { "Tempo Nudge",    OUTPUT_TYPE_TEMPO_NUDGE, true  },
};

static inline const als_mode_mapping_t* als_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_ALS_USER_MODES) return NULL;
  return &g_als_mode_mappings[user_mode_index];
}

static inline const char* als_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_ALS_USER_MODES) return "Unknown";
  return g_als_mode_mappings[user_mode_index].display_name;
}

static inline uint8_t als_get_current_mode_index(const scene_t* scene) {
  if (!scene || !scene->als.enabled) return 0;

  output_type_t output = scene->als.output_type;
  for (uint8_t i = 1; i < NUM_ALS_USER_MODES; i++) {
    if (g_als_mode_mappings[i].output_type == output) return i;
  }
  return 1;  // Default to Control Change
}

#endif // ALS_MODE_MAPPING_H
