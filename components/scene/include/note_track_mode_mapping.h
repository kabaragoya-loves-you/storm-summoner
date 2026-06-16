#ifndef NOTE_TRACK_MODE_MAPPING_H
#define NOTE_TRACK_MODE_MAPPING_H

#include "scene.h"

typedef struct {
  const char* display_name;
  output_type_t output_type;
  bool enabled;
} note_track_mode_mapping_t;

#define NUM_NOTE_TRACK_USER_MODES 6

static const note_track_mode_mapping_t g_note_track_mode_mappings[] = {
  { "Disabled",       OUTPUT_TYPE_CC,          false },
  { "Control Change", OUTPUT_TYPE_CC,          true  },
  { "LFO Rate",       OUTPUT_TYPE_LFO_RATE,    true  },
  { "LFO Depth",      OUTPUT_TYPE_LFO_DEPTH,   true  },
  { "Pitch Bend",     OUTPUT_TYPE_PITCH_BEND,  true  },
  { "Tempo Nudge",    OUTPUT_TYPE_TEMPO_NUDGE, true  },
};

static inline const note_track_mode_mapping_t* note_track_get_mode_mapping(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_NOTE_TRACK_USER_MODES) return NULL;
  return &g_note_track_mode_mappings[user_mode_index];
}

static inline const char* note_track_get_mode_name(uint8_t user_mode_index) {
  if (user_mode_index >= NUM_NOTE_TRACK_USER_MODES) return "Unknown";
  return g_note_track_mode_mappings[user_mode_index].display_name;
}

static inline uint8_t note_track_get_current_mode_index(const scene_t* scene) {
  if (!scene || !scene->note_track.enabled) return 0;

  output_type_t output = scene->note_track.output_type;
  for (uint8_t i = 1; i < NUM_NOTE_TRACK_USER_MODES; i++) {
    if (g_note_track_mode_mappings[i].output_type == output) return i;
  }
  return 1;
}

#endif // NOTE_TRACK_MODE_MAPPING_H
