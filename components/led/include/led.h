#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// LED operational modes
typedef enum {
  LED_MODE_DAYLIGHT,    // Off by default, turn on explicitly
  LED_MODE_NIGHTTIME    // On by default, turn off explicitly (inverted)
} led_mode_t;

void led_init(void);

// Flicker mode (ambient randomized bursts)
void flicker_start(void);
void flicker_stop(void);
bool flicker_is_running(void);
bool led_get_flicker_preference(void);  // Check if user wants flicker (persisted)

// Direct LED control
void flash_led(uint32_t duration);  // Flash for duration (non-blocking)
void led_set_on(void);              // Turn LED on (solid)
void led_set_off(void);             // Turn LED off

// Day/Night mode
esp_err_t led_set_mode(led_mode_t mode);
led_mode_t led_get_mode(void);

// Sundial mode - auto-switch day/night based on ambient light sensor
// Note: Requires sensor_init() and als_enable() to be called
esp_err_t led_set_sundial_mode(bool enabled);
bool led_get_sundial_mode(void);

// Global enable/disable
void led_set_enabled(bool enabled);
bool led_get_enabled(void);

#endif // LED_H