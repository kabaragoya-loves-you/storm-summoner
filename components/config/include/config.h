#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "esp_err.h"

// Initialize config module (loads settings from NVS)
esp_err_t config_init(void);

// Program wrap setting (for program change mode)
// When true: program numbers wrap around (127->0, 0->127)
// When false: program numbers clamp at boundaries
bool config_get_program_wrap(void);
esp_err_t config_set_program_wrap(bool wrap);

#endif // CONFIG_H

