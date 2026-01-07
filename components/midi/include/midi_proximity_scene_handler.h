#ifndef MIDI_PROXIMITY_SCENE_HANDLER_H
#define MIDI_PROXIMITY_SCENE_HANDLER_H

#include "esp_err.h"

// Initialize proximity sensor scene-based routing
esp_err_t midi_proximity_scene_handler_init(void);

// Release any active proximity note (for Notes output mode)
void midi_proximity_scene_handler_release_notes(void);

#endif // MIDI_PROXIMITY_SCENE_HANDLER_H

