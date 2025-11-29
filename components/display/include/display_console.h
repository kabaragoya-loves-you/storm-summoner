#ifndef DISPLAY_CONSOLE_H
#define DISPLAY_CONSOLE_H

#include "esp_err.h"
#include <stdint.h>

/**
 * Initialize display console commands
 * Also initializes backlight PWM control
 */
esp_err_t display_console_init(void);

/**
 * Cleanup display console commands
 */
void display_console_cleanup(void);

/**
 * Set display backlight brightness
 * 
 * @param percent Brightness level 0-100%
 * @return ESP_OK on success
 */
esp_err_t display_set_brightness(uint8_t percent);

/**
 * Get current display backlight brightness
 * 
 * @return Current brightness level 0-100%
 */
uint8_t display_get_brightness(void);

#endif // DISPLAY_CONSOLE_H
