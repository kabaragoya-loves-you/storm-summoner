#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Display Driver Interface
 * 
 * Provides the interface for the ST7789V3 240x240 RGB color IPS display.
 */

// Display driver interface structure
typedef struct {
  const char *name;
  uint16_t width;
  uint16_t height;
  lv_color_format_t color_format;
  void (*init)(void);
  void (*flush)(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
} display_driver_t;

/**
 * Get the display driver
 * 
 * @return Pointer to the display driver
 */
const display_driver_t *display_driver_get(void);

/**
 * Initialize the display driver
 * Call this before display_init()
 */
void display_driver_select(void);

/**
 * Get display width in pixels
 * 
 * @return Width of the display (viewport width)
 */
uint16_t display_get_width(void);

/**
 * Get display height in pixels
 * 
 * @return Height of the display (viewport height)
 */
uint16_t display_get_height(void);

/**
 * Get the color format used by the display
 * 
 * @return LVGL color format constant
 */
lv_color_format_t display_get_color_format(void);

/**
 * Check if the display is circular (round)
 * 
 * @return true (ST7789V3 is viewed through a circular aperture)
 */
bool display_is_circular(void);

// Driver declaration
extern const display_driver_t st7789v3_driver;

#endif // DISPLAY_DRIVER_H
