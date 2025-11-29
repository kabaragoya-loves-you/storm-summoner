#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Display Driver Abstraction Layer
 * 
 * Provides a common interface for different display controllers:
 * - SSD1327: 128x128 4-bit greyscale OLED
 * - GC9A01A: 240x240 RGB color IPS
 * 
 * The active driver is selected at runtime based on hardware revision.
 */

// Display type enumeration
typedef enum {
  DISPLAY_TYPE_SSD1327,   // 128x128 greyscale OLED
  DISPLAY_TYPE_GC9A01A,   // 240x240 color IPS
  DISPLAY_TYPE_UNKNOWN
} display_type_t;

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
 * Get the current display driver (selected based on hardware revision)
 * Must be called after revision_init() and display_driver_init()
 * 
 * @return Pointer to the active display driver, or NULL if not initialized
 */
const display_driver_t *display_driver_get(void);

/**
 * Initialize the display driver selection based on hardware revision
 * Call this after revision_init() but before display_init()
 */
void display_driver_select(void);

/**
 * Get the display type currently in use
 * 
 * @return Display type enum value
 */
display_type_t display_driver_get_type(void);

/**
 * Get display width in pixels
 * 
 * @return Width of the active display
 */
uint16_t display_get_width(void);

/**
 * Get display height in pixels
 * 
 * @return Height of the active display
 */
uint16_t display_get_height(void);

/**
 * Get the color format used by the active display
 * 
 * @return LVGL color format constant
 */
lv_color_format_t display_get_color_format(void);

/**
 * Check if the active display is circular (round)
 * Used to determine if circular clipping/masking is needed
 * 
 * @return true if display is circular
 */
bool display_is_circular(void);

// External driver declarations
extern const display_driver_t ssd1327_driver;
extern const display_driver_t gc9a01a_driver;

#endif // DISPLAY_DRIVER_H

