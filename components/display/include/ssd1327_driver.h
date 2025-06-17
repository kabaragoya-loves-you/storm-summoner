#ifndef SSD1327_H
#define SSD1327_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

void ssd1327_init(void);
void ssd1327_flush(lv_display_t *drv, const lv_area_t *area, uint8_t * px_map);



// Circular display optimization function
// bool is_pixel_visible(int16_t x, int16_t y);

#endif