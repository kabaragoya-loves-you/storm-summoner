#ifndef FIRMWARE_UPDATE_CONSOLE_H
#define FIRMWARE_UPDATE_CONSOLE_H

#include "esp_err.h"

esp_err_t firmware_update_console_init(void);
void firmware_update_console_cleanup(void);

#endif // FIRMWARE_UPDATE_CONSOLE_H

