#ifndef MIDI_SAMPLE_HOLD_SCENE_HANDLER_H
#define MIDI_SAMPLE_HOLD_SCENE_HANDLER_H

#include "esp_err.h"

// Initialize the Sample+Hold scene handler (subscribes to EVENT_SAMPLE_HOLD_VALUE)
esp_err_t midi_sample_hold_scene_handler_init(void);

#endif // MIDI_SAMPLE_HOLD_SCENE_HANDLER_H
