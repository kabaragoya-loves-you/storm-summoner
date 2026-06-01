#ifndef MIDI_CONTROL_H
#define MIDI_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MIDI_CONTROL_DEFAULT_CHANNEL 16

typedef enum {
  MIDI_CONTROL_INPUT_TRS = 0,
  MIDI_CONTROL_INPUT_USB = 1,
  MIDI_CONTROL_INPUT_BOTH = 2,
} midi_control_input_t;

esp_err_t midi_control_init(void);

bool midi_control_is_enabled(void);
esp_err_t midi_control_set_enabled(bool enabled);

uint8_t midi_control_get_channel(void);
esp_err_t midi_control_set_channel(uint8_t channel);

midi_control_input_t midi_control_get_input(void);
esp_err_t midi_control_set_input(midi_control_input_t input);

#endif // MIDI_CONTROL_H
