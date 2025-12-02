#ifndef GC9A01A_DRIVER_H
#define GC9A01A_DRIVER_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

/**
 * GC9A01A 240x240 Round IPS Display Driver
 * 
 * This driver supports the GC9A01A display controller commonly found in
 * round 1.28" 240x240 IPS LCD modules. It uses SPI for communication.
 * 
 * Color format: RGB888 (24-bit color)
 */

// Physical display dimensions
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

/**
 * Viewport configuration for aperture calibration
 * The viewport defines the visible area of the display through the aperture
 */
void gc9a01a_set_viewport(int16_t offset_x, int16_t offset_y, uint16_t width, uint16_t height);
uint16_t gc9a01a_get_viewport_width(void);
uint16_t gc9a01a_get_viewport_height(void);
int16_t gc9a01a_get_viewport_offset_x(void);
int16_t gc9a01a_get_viewport_offset_y(void);

#endif // GC9A01A_DRIVER_H

