#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Configuration
#define BUTTON_DEBOUNCE_MS_DEFAULT 3
#define BUTTON_DEBOUNCE_MS_MIN 0
#define BUTTON_DEBOUNCE_MS_MAX 100
#define BUTTON_LONG_PRESS_MS_DEFAULT 500
#define BUTTON_LONG_PRESS_MS_MIN 100
#define BUTTON_LONG_PRESS_MS_MAX 5000
#define BUTTON_CHORD_WINDOW_MS_DEFAULT 50
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

/**
 * Glitch filter modes for hardware debouncing
 * 0 = None (software debounce + hysteresis only)
 * 1 = Simple (filters very short pulses based on APB clock cycles)
 * 2 = Flex (configurable window in nanoseconds)
 */
#define BUTTON_GLITCH_FILTER_MODE_NONE   0
#define BUTTON_GLITCH_FILTER_MODE_SIMPLE 1
#define BUTTON_GLITCH_FILTER_MODE_FLEX   2

/**
 * Get the current glitch filter mode
 * 
 * @return Current mode (0=none, 1=simple, 2=flex)
 */
uint8_t buttons_get_glitch_filter_mode(void);

/**
 * Get the current glitch filter window for flex mode
 * 
 * @return Window in nanoseconds
 */
uint32_t buttons_get_glitch_filter_window_ns(void);

/**
 * Set the glitch filter mode and window
 * Changes take effect immediately. Setting is saved to NVS.
 * 
 * @param mode Filter mode (0=none, 1=simple, 2=flex)
 * @param window_ns Window in nanoseconds (only used for flex mode, 100-4000ns)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_glitch_filter(uint8_t mode, uint32_t window_ns);

/**
 * Debounce strategy modes
 * 0 = Symmetric: same debounce for press and release (default)
 * 1 = Asymmetric: full debounce on press, reduced on release (default 1ms)
 */
#define BUTTON_DEBOUNCE_MODE_SYMMETRIC    0
#define BUTTON_DEBOUNCE_MODE_ASYMMETRIC   1

/**
 * Get the current debounce mode
 * 
 * @return Current mode (0=symmetric, 1=asymmetric)
 */
uint8_t buttons_get_debounce_mode(void);

/**
 * Get the release debounce time (for asymmetric mode)
 * 
 * @return Release debounce in milliseconds
 */
uint16_t buttons_get_debounce_release(void);

/**
 * Set the debounce mode
 * Changes take effect immediately. Setting is saved to NVS.
 * 
 * @param mode Debounce mode (0=symmetric, 1=asymmetric)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_debounce_mode(uint8_t mode);

/**
 * Set the release debounce time (for asymmetric mode)
 * Changes take effect immediately. Setting is saved to NVS.
 * 
 * @param release_ms Release debounce in milliseconds (0-100)
 * @return ESP_OK on success
 */
esp_err_t buttons_set_debounce_release(uint16_t release_ms);

#endif // BUTTONS_H


