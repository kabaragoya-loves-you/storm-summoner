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

// Persist scene setting
// When true: the current scene index is saved to NVS and restored on boot
// When false: device always boots to scene 1
bool config_get_persist_scene(void);
esp_err_t config_set_persist_scene(bool persist);

// Last scene index (used when persist_scene is enabled)
uint8_t config_get_last_scene(void);
esp_err_t config_set_last_scene(uint8_t scene_index);

#endif // CONFIG_H

