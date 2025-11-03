#ifndef MIDI_SEND_CONSOLE_H
#define MIDI_SEND_CONSOLE_H

#include "esp_err.h"

// Register the global 'send' command
// This is called from console_repl_init() to make it available everywhere
esp_err_t midi_send_console_register(void);

#endif // MIDI_SEND_CONSOLE_H

