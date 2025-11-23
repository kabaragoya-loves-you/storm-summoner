#ifndef TOUCHWHEEL_CORE_H
#define TOUCHWHEEL_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Include touchwheel_modes.h first to get the enum and NUM_WHEEL_PADS
// Use include guard check to prevent multiple definitions
#ifndef TOUCHWHEEL_MODES_H
#include "touchwheel_modes.h"
#endif

// If touchwheel_modes.h wasn't included, define NUM_WHEEL_PADS here
#ifndef NUM_WHEEL_PADS
#define NUM_WHEEL_PADS 8
#endif

// Callback type for delta notifications
typedef void (*touchwheel_delta_cb_t)(int delta, int position, uint32_t timestamp_ms, void* user_data);

// Callback type for position-based value setting (for multi-pad releases)
typedef void (*touchwheel_position_cb_t)(int value, uint32_t timestamp_ms, void* user_data);

// Core touchwheel driver state
typedef struct {
  int last_logical_wheel_pos;
  uint32_t last_wheel_interaction_time;
  uint32_t last_pad_touch_time;
  bool interaction_active;
  bool pad_pressed_states[NUM_WHEEL_PADS];
  uint32_t inactivity_timeout_ms;
  
  // Mode-specific tracking
  touchwheel_mode_type_t mode_type;
  uint8_t active_pads[NUM_WHEEL_PADS];  // Array of currently active pad IDs
  int num_active_pads;                  // Count of active pads
  uint32_t pad_release_times[NUM_WHEEL_PADS];  // Timestamps for release detection
  bool boundary_violated;              // True if boundary was violated in current interaction
  int boundary_violation_direction;     // Direction of boundary violation (+1 or -1)
  
  touchwheel_delta_cb_t delta_callback;
  touchwheel_position_cb_t position_callback;  // For multi-pad position-based value setting
  void* callback_user_data;
  
  // Analog position tracking
  float last_analog_position;  // Last analog position (0.0-7.999)
  bool analog_mode_active;     // True if analog sampling is active
} touchwheel_core_t;

/**
 * Initialize touchwheel core driver
 * @param core Pointer to core structure (caller allocates)
 * @param mode_type Mode type (endless, odometer, bipolar)
 * @param inactivity_timeout_ms Timeout in ms before resetting interaction state
 * @return ESP_OK on success
 */
esp_err_t touchwheel_core_init(touchwheel_core_t* core, touchwheel_mode_type_t mode_type, uint32_t inactivity_timeout_ms);

/**
 * Process pad press event
 * @param core Core driver instance
 * @param pad_id Pad ID (0-7 for wheel pads)
 * @param timestamp_ms Current timestamp in milliseconds
 */
void touchwheel_core_process_press(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms);

/**
 * Process analog position update
 * @param core Core driver instance
 * @param analog_position Interpolated position (0.0 to 7.999)
 * @param timestamp_ms Current timestamp in milliseconds
 */
void touchwheel_core_process_analog_position(touchwheel_core_t* core, float analog_position, uint32_t timestamp_ms);

/**
 * Process pad release event
 * @param core Core driver instance
 * @param pad_id Pad ID (0-7 for wheel pads)
 * @param timestamp_ms Current timestamp in milliseconds
 * @param released_pads_out Optional output array for released pad IDs (caller allocates, size NUM_WHEEL_PADS)
 * @param num_released_out Optional output for number of released pads
 */
void touchwheel_core_process_release(touchwheel_core_t* core, uint8_t pad_id, uint32_t timestamp_ms, uint8_t* released_pads_out, int* num_released_out);

/**
 * Set delta callback
 * @param core Core driver instance
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void touchwheel_core_set_callback(touchwheel_core_t* core, touchwheel_delta_cb_t callback, void* user_data);

/**
 * Set position callback (for multi-pad position-based value setting)
 * @param core Core driver instance
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void touchwheel_core_set_position_callback(touchwheel_core_t* core, touchwheel_position_cb_t callback, void* user_data);

/**
 * Check if a pad is contiguous with currently active pads
 * @param core Core driver instance
 * @param pad_id Pad ID to check
 * @return true if pad is adjacent to active set
 */
bool touchwheel_core_is_pad_contiguous(const touchwheel_core_t* core, uint8_t pad_id);

/**
 * Check if a pad transition violates boundary constraints
 * @param core Core driver instance
 * @param from_pad Source pad ID
 * @param to_pad Destination pad ID
 * @return true if transition violates boundaries
 */
bool touchwheel_core_is_boundary_violation(const touchwheel_core_t* core, uint8_t from_pad, uint8_t to_pad);

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
