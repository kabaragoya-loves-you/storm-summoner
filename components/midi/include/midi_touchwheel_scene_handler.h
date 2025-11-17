#ifndef MIDI_TOUCHWHEEL_SCENE_HANDLER_H
#define MIDI_TOUCHWHEEL_SCENE_HANDLER_H

#include "esp_err.h"

/**
 * Initialize touchwheel scene handler
 * Subscribes to EVENT_TOUCHWHEEL_VALUE and routes through scene touchwheel_actions
 * @return ESP_OK on success
 */
esp_err_t midi_touchwheel_scene_handler_init(void);

#endif // MIDI_TOUCHWHEEL_SCENE_HANDLER_H


