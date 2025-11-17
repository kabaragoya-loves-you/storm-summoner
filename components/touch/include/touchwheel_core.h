#ifndef TOUCHWHEEL_CORE_H
#define TOUCHWHEEL_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define NUM_WHEEL_PADS 8

// Callback type for delta notifications
typedef void (*touchwheel_delta_cb_t)(int delta, int position, uint32_t timestamp_ms, void* user_data);

// Core touchwheel driver state
typedef struct {
  int last_logical_wheel_pos;
  uint32_t last_wheel_interaction_time;
  uint32_t last_pad_touch_time;
  bool interaction_active;
  bool pad_pressed_states[NUM_WHEEL_PADS];
  uint32_t inactivity_timeout_ms;
  
  touchwheel_delta_cb_t delta_callback;
  void* callback_user_data;
} touchwheel_core_t;

/**
 * Initialize touchwheel core driver
 * @param core Pointer to core structure (caller allocates)
 * @param inactivity_timeout_ms Timeout in ms before resetting interaction state
 * @return ESP_OK on success
 */
esp_err_t touchwheel_core_init(touchwheel_core_t* core, uint32_t inactivity_timeout_ms);

/**
 * Process pad press event
 * @param core Core driver instance
 * @param pad_id Pad ID (0-7 for wheel pads)
 * @param timestamp_ms Current timestamp in milliseconds
 */
void touchwheel_core_process_press(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms);

/**
 * Process pad release event
 * @param core Core driver instance
 * @param pad_id Pad ID (0-7 for wheel pads)
 * @param timestamp_ms Current timestamp in milliseconds
 */
void touchwheel_core_process_release(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms);

/**
 * Set delta callback
 * @param core Core driver instance
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void touchwheel_core_set_callback(touchwheel_core_t* core, touchwheel_delta_cb_t callback, void* user_data);

/**
 * Reset touchwheel state (clears position tracking)
 * @param core Core driver instance
 */
void touchwheel_core_reset(touchwheel_core_t* core);

/**
 * Check if any wheel pads are currently pressed
 * @param core Core driver instance
 * @return true if any pad is pressed
 */
bool touchwheel_core_are_any_pads_pressed(const touchwheel_core_t* core);

#endif // TOUCHWHEEL_CORE_H


