#ifndef SCENE_ASSIGN_CONSOLE_H
#define SCENE_ASSIGN_CONSOLE_H

#include "esp_err.h"

// Initialize assignment console context
esp_err_t scene_assign_console_init(void);

// Cleanup assignment console context
void scene_assign_console_cleanup(void);

#endif // SCENE_ASSIGN_CONSOLE_H

