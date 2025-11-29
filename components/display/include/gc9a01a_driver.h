#ifndef GC9A01A_DRIVER_H
#define GC9A01A_DRIVER_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

/**
 * GC9A01A 240x240 Round IPS Display Driver
 * 
 * This driver supports the GC9A01A display controller commonly found in
 * round 1.28" 240x240 IPS LCD modules. It uses SPI for communication
 * with the same pinout as the SSD1327 OLED.
 * 
 * Color format: RGB888 (24-bit color)
 */

// Display dimensions
#define GC9A01A_WIDTH  240
#define GC9A01A_HEIGHT 240

/**
 * Initialize the GC9A01A display controller
 * Configures SPI, performs reset sequence, and sends initialization commands
 */
void gc9a01a_init(void);

/**
 * Flush pixels to the GC9A01A display
 * Called by LVGL when a display area needs to be updated
 * 
 * @param disp   LVGL display handle
 * @param area   Area to update (x1, y1) to (x2, y2)
 * @param px_map Pixel data in LVGL format
 */
void gc9a01a_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#endif // GC9A01A_DRIVER_H

