#ifndef MIDI_CONSOLE_H
#define MIDI_CONSOLE_H

#include "esp_err.h"

esp_err_t midi_console_init(void);
void midi_console_cleanup(void);

#endif // MIDI_CONSOLE_H

