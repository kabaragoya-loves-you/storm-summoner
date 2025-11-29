#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Configuration
#define BUTTON_DEBOUNCE_MS_DEFAULT 10
#define BUTTON_DEBOUNCE_MS_MIN 0
#define BUTTON_DEBOUNCE_MS_MAX 100
#define BUTTON_LONG_PRESS_MS_DEFAULT 500
#define BUTTON_LONG_PRESS_MS_MIN 100
#define BUTTON_LONG_PRESS_MS_MAX 5000
#define BUTTON_CHORD_WINDOW_MS_DEFAULT 100
#define BUTTON_CHORD_WINDOW_MS_MIN 0
#define BUTTON_CHORD_WINDOW_MS_MAX 500

// Button IDs
#define BUTTON_ID_LEFT 0
#define BUTTON_ID_RIGHT 1
#define BUTTON_ID_BOTH 2

// Button state structure
typedef struct {
  bool left_pressed;
  bool right_pressed;
  bool both_pressed;
} button_state_t;

/**
 * Initialize the buttons component
 * Configures GPIO pins, ISRs, and timers
 * 
 * @param enable_logging Enable verbose logging of button events
 */
esp_err_t buttons_init(bool enable_logging);

/**
 * Get current button states
 * Useful for debugging and status queries
 */
button_state_t buttons_get_state(void);

/**
 * Set the chord detection window in milliseconds
 * This is the delay before a single button press event is fired,
 * allowing time for a second button to be pressed for chord detection.
 * Set to 0 to disable chord detection (immediate single button events).
 * 
 * @param window_ms Chord window in milliseconds (0-500)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_chord_window(uint16_t window_ms);

/**
 * Get the current chord detection window in milliseconds
 * 
 * @return Chord window in milliseconds
 */
uint16_t buttons_get_chord_window(void);

/**
 * Set the debounce delay in milliseconds
 * This is the minimum time between button state changes.
 * 
 * @param debounce_ms Debounce delay in milliseconds (0-100)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_debounce(uint16_t debounce_ms);

/**
 * Get the current debounce delay in milliseconds
 * 
 * @return Debounce delay in milliseconds
 */
uint16_t buttons_get_debounce(void);

/**
 * Set the long press threshold in milliseconds
 * This is the duration a button must be held to trigger a long press event.
 * 
 * @param long_press_ms Long press threshold in milliseconds (100-5000)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_long_press_threshold(uint16_t long_press_ms);

/**
 * Get the current long press threshold in milliseconds
 * 
 * @return Long press threshold in milliseconds
 */
uint16_t buttons_get_long_press_threshold(void);

/**
 * Check if the Right button is held during boot
 * Must be called BEFORE buttons_init() for accurate reading.
 * Configures GPIO temporarily to read the button state.
 * 
 * @return true if Right button is pressed (held) at boot
 */
bool buttons_check_boot_right(void);

#endif // BUTTONS_H


