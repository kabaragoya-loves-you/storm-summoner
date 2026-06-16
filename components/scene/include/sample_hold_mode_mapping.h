#ifndef SAMPLE_HOLD_MODE_MAPPING_H
#define SAMPLE_HOLD_MODE_MAPPING_H

#include "scene.h"
#include "sample_hold.h"

typedef struct {
  const char* display_name;
  sample_hold_mode_t mode;
  bool enabled;
} sample_hold_mode_mapping_t;

#define NUM_SAMPLE_HOLD_USER_MODES 3

static const sample_hold_mode_mapping_t g_sample_hold_mode_mappings[] = {
  { "Disabled",   SAMPLE_HOLD_MODE_CONTINUOUS, false },
  { "Continuous", SAMPLE_HOLD_MODE_CONTINUOUS, true  },
  { "Step",       SAMPLE_HOLD_MODE_STEP,       true  },
};

static inline const sample_hold_mode_mapping_t* sample_hold_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_SAMPLE_HOLD_USER_MODES) return NULL;
  return &g_sample_hold_mode_mappings[user_mode_index];
}

static inline const char* sample_hold_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_SAMPLE_HOLD_USER_MODES) return "Unknown";
  return g_sample_hold_mode_mappings[user_mode_index].display_name;
}

static inline uint8_t sample_hold_get_current_mode_index(const scene_t* scene) {
  if (!scene || !scene->sample_hold_config.enabled) return 0;
  return (scene->sample_hold_config.mode == SAMPLE_HOLD_MODE_STEP) ? 2 : 1;
}

#endif // SAMPLE_HOLD_MODE_MAPPING_H
