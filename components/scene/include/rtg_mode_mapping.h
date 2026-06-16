#ifndef RTG_MODE_MAPPING_H
#define RTG_MODE_MAPPING_H

#include "scene.h"
#include "rtg.h"

typedef struct {
  const char* display_name;
  rtg_mode_t mode;
  bool enabled;
} rtg_mode_mapping_t;

#define NUM_RTG_USER_MODES 3

static const rtg_mode_mapping_t g_rtg_mode_mappings[] = {
  { "Disabled",   RTG_MODE_CONTINUOUS, false },
  { "Continuous", RTG_MODE_CONTINUOUS, true  },
  { "Step",       RTG_MODE_STEP,       true  },
};

static inline const rtg_mode_mapping_t* rtg_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_RTG_USER_MODES) return NULL;
  return &g_rtg_mode_mappings[user_mode_index];
}

static inline const char* rtg_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_RTG_USER_MODES) return "Unknown";
  return g_rtg_mode_mappings[user_mode_index].display_name;
}

static inline uint8_t rtg_get_current_mode_index(const scene_t* scene) {
  if (!scene || !scene->rtg_config.enabled) return 0;
  return (scene->rtg_config.mode == RTG_MODE_STEP) ? 2 : 1;
}

#endif // RTG_MODE_MAPPING_H
