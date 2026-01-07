#ifndef MIDI_CV_SCENE_HANDLER_H
#define MIDI_CV_SCENE_HANDLER_H

#include "esp_err.h"

esp_err_t midi_cv_scene_handler_init(void);

/**
 * Release any active CV note (for Notes output mode)
 * Call when entering programming mode to prevent stuck notes
 */
void midi_cv_scene_handler_release_notes(void);

#endif // MIDI_CV_SCENE_HANDLER_H

