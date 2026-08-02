#ifndef SAMPLE_HOLD_CONSOLE_H
#define SAMPLE_HOLD_CONSOLE_H

#include "esp_err.h"

// Register Sample+Hold console commands
esp_err_t sample_hold_console_init(void);

// Cleanup Sample+Hold console commands
void sample_hold_console_cleanup(void);

#endif // SAMPLE_HOLD_CONSOLE_H
