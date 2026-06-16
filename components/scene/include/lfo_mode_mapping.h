#ifndef LFO_MODE_MAPPING_H
#define LFO_MODE_MAPPING_H

#include "scene.h"

// User-facing LFO mode mapping (per slot: 0 = LFO1, 1 = LFO2).
// Flattens lfoN_config.enabled + lfoN.output_type into one Mode list.
typedef struct {
  const char* display_name;
  output_type_t output_type;
  bool enabled;
} lfo_mode_mapping_t;

#define NUM_LFO_USER_MODES 8

static inline const lfo_mode_mapping_t* lfo_get_mode_mappings(uint8_t lfo_slot) {
  static const lfo_mode_mapping_t lfo1_mappings[] = {
    { "Disabled",    OUTPUT_TYPE_CC,          false },
    { "Control Change", OUTPUT_TYPE_CC,       true  },
    { "Notes",       OUTPUT_TYPE_NOTE,        true  },
    { "LFO2 Rate",   OUTPUT_TYPE_LFO2_RATE,   true  },
    { "LFO2 Depth",  OUTPUT_TYPE_LFO2_DEPTH,  true  },
    { "RTG Rate",    OUTPUT_TYPE_RTG_RATE,    true  },
    { "S+H Rate",    OUTPUT_TYPE_SH_RATE,     true  },
    { "Pitch Bend",  OUTPUT_TYPE_PITCH_BEND,  true  },
  };
  static const lfo_mode_mapping_t lfo2_mappings[] = {
    { "Disabled",    OUTPUT_TYPE_CC,          false },
    { "Control Change", OUTPUT_TYPE_CC,       true  },
    { "Notes",       OUTPUT_TYPE_NOTE,        true  },
    { "LFO1 Rate",   OUTPUT_TYPE_LFO1_RATE,   true  },
    { "LFO1 Depth",  OUTPUT_TYPE_LFO1_DEPTH,  true  },
    { "RTG Rate",    OUTPUT_TYPE_RTG_RATE,    true  },
    { "S+H Rate",    OUTPUT_TYPE_SH_RATE,     true  },
    { "Pitch Bend",  OUTPUT_TYPE_PITCH_BEND,  true  },
  };
  return (lfo_slot == 0) ? lfo1_mappings : lfo2_mappings;
}

static inline const lfo_mode_mapping_t* lfo_get_mode_mapping(uint8_t lfo_slot, uint8_t user_mode_index) {
  if (user_mode_index >= NUM_LFO_USER_MODES) return NULL;
  return &lfo_get_mode_mappings(lfo_slot)[user_mode_index];
}

static inline const char* lfo_get_mode_name(uint8_t lfo_slot, uint8_t user_mode_index) {
  const lfo_mode_mapping_t* m = lfo_get_mode_mapping(lfo_slot, user_mode_index);
  return m ? m->display_name : "Unknown";
}

static inline uint8_t lfo_get_current_mode_index(uint8_t lfo_slot, const scene_t* scene) {
  if (!scene) return 0;

  bool enabled = (lfo_slot == 0) ? scene->lfo1_config.enabled : scene->lfo2_config.enabled;
  if (!enabled) return 0;

  output_type_t output = (lfo_slot == 0) ? scene->lfo1.output_type : scene->lfo2.output_type;
  const lfo_mode_mapping_t* mappings = lfo_get_mode_mappings(lfo_slot);
  for (uint8_t i = 1; i < NUM_LFO_USER_MODES; i++) {
    if (mappings[i].output_type == output) return i;
  }
  return 1;
}

#endif // LFO_MODE_MAPPING_H
