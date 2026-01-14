#ifndef LFO_CONSOLE_H
#define LFO_CONSOLE_H

#include "esp_err.h"

// Register LFO console commands
esp_err_t lfo_console_init(void);

// Cleanup LFO console commands
void lfo_console_cleanup(void);

#endif // LFO_CONSOLE_H
