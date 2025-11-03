#ifndef SCENE_CONSOLE_H
#define SCENE_CONSOLE_H

#include "esp_err.h"

// Initialize scene console context (register commands)
esp_err_t scene_console_init(void);

// Cleanup scene console context (unregister commands)
void scene_console_cleanup(void);

#endif // SCENE_CONSOLE_H

