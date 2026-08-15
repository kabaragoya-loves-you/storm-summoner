#ifndef MIDI_PROXIMITY_SCENE_HANDLER_H
#define MIDI_PROXIMITY_SCENE_HANDLER_H

#include "esp_err.h"

// Initialize proximity sensor scene-based routing
esp_err_t midi_proximity_scene_handler_init(void);

// Release any active proximity note (for Notes output mode)
void midi_proximity_scene_handler_release_notes(void);

// Re-apply rest CC after device proximity settings or scene rest change
void midi_proximity_scene_handler_proximity_settings_changed(void);

// Local MIDI output just came back; start rest return if the field is idle
void midi_proximity_scene_handler_on_output_enabled(void);

#endif // MIDI_PROXIMITY_SCENE_HANDLER_H

