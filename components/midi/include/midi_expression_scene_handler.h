#ifndef MIDI_EXPRESSION_SCENE_HANDLER_H
#define MIDI_EXPRESSION_SCENE_HANDLER_H

#include "esp_err.h"

/**
 * Initialize MIDI expression scene handler
 * Subscribes to sustain and sostenuto pedal events
 * and executes scene-assigned action chains
 */
esp_err_t midi_expression_scene_handler_init(void);

#endif // MIDI_EXPRESSION_SCENE_HANDLER_H
