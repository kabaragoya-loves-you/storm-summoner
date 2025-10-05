#ifndef MIDI_SCENE_HANDLER_H
#define MIDI_SCENE_HANDLER_H

#include "esp_err.h"

// Initialize the MIDI scene handler
// This sets up the scene manager and subscribes to touch events
esp_err_t midi_scene_handler_init(void);

#endif // MIDI_SCENE_HANDLER_H


