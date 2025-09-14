#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "lvgl.h"
#include <stdint.h>

// Background configuration
typedef struct {
  lv_color_t color;  // Background color
  lv_opa_t opacity;  // Opacity (0-255)
} background_config_t;

// Initialize background with black color
void background_init(void);

// Initialize background with custom color
void background_init_with_config(const background_config_t* config);

// Draw background
void background_draw(lv_obj_t* canvas);

// Deinit (for consistency, though minimal cleanup needed)
void background_deinit(void);

#endif // BACKGROUND_H
