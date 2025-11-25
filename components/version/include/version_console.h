#ifndef VERSION_CONSOLE_H
#define VERSION_CONSOLE_H

#include "esp_err.h"

/**
 * Register version console commands
 * @return ESP_OK on success
 */
esp_err_t version_console_init(void);

/**
 * Unregister version console commands
 */
void version_console_cleanup(void);

#endif // VERSION_CONSOLE_H

