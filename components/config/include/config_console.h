#ifndef CONFIG_CONSOLE_H
#define CONFIG_CONSOLE_H

#include "esp_err.h"

// Initialize config console commands
esp_err_t config_console_init(void);

// Cleanup config console
void config_console_cleanup(void);

#endif // CONFIG_CONSOLE_H

