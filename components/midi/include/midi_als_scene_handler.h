#ifndef MIDI_ALS_SCENE_HANDLER_H
#define MIDI_ALS_SCENE_HANDLER_H

#include "esp_err.h"

// Initialize ALS sensor scene-based routing
esp_err_t midi_als_scene_handler_init(void);

// Release any active ALS note (for Notes output mode)
void midi_als_scene_handler_release_notes(void);

#endif // MIDI_ALS_SCENE_HANDLER_H

