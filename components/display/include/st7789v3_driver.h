#ifndef ST7789V3_DRIVER_H
#define ST7789V3_DRIVER_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"

/**
 * ST7789V3 240x240 IPS Display Driver
 * 
 * This driver supports the ST7789V3 display controller commonly found in
 * 1.3" 240x240 IPS LCD modules. It uses SPI for communication.
 * 
 * Color format: RGB565 (16-bit color)
 */

// Physical display dimensions
#define ST7789V3_WIDTH  240
#define ST7789V3_HEIGHT 240

/**
 * Initialize the ST7789V3 display controller
 * Configures SPI, performs reset sequence, and sends initialization commands
 */
void st7789v3_init(void);

/**
 * Flush pixels to the ST7789V3 display
 * Called by LVGL when a display area needs to be updated
 * 
 * @param disp   LVGL display handle
 * @param area   Area to update (x1, y1) to (x2, y2)
 * @param px_map Pixel data in LVGL format
 */
void st7789v3_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/**
 * Viewport configuration for display calibration
 * The viewport defines the visible area of the display
 * For ST7789V3 centered in aperture, offset is (0,0) and size is 240x240
 */
void st7789v3_set_viewport(int16_t offset_x, int16_t offset_y, uint16_t width, uint16_t height);
uint16_t st7789v3_get_viewport_width(void);
uint16_t st7789v3_get_viewport_height(void);
int16_t st7789v3_get_viewport_offset_x(void);
int16_t st7789v3_get_viewport_offset_y(void);

#endif // ST7789V3_DRIVER_H
