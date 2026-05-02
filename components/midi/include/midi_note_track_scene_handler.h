#ifndef MIDI_NOTE_TRACK_SCENE_HANDLER_H
#define MIDI_NOTE_TRACK_SCENE_HANDLER_H

#include "esp_err.h"

/**
 * Initialize the Note Track scene handler.
 *
 * Subscribes to EVENT_MIDI_IN, converts incoming Note On numbers within the
 * configured global range (defaults to C2..C6) into 0-127 values, and feeds
 * them through the active scene's note_track continuous_mapping_t.
 */
esp_err_t midi_note_track_scene_handler_init(void);

#endif // MIDI_NOTE_TRACK_SCENE_HANDLER_H
