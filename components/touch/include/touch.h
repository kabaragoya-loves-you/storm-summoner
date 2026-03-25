#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/touch_sens.h"

#define MAX_TOUCH_PADS 13

// One channel may have inverted polarity (value decreases when touched instead of increasing)
// Set to -1 to disable inversion handling entirely
#if HW_CONFIG_PRODUCTION
#define INVERTED_TOUCH_CHANNEL (-1)  // No inverted channel on production hardware
#elif HW_CONFIG_DEV_BOARD
#define INVERTED_TOUCH_CHANNEL 14    // Channel 14 is inverted on dev board
#else
#define INVERTED_TOUCH_CHANNEL (-1)  // Default: no inversion
#endif

typedef int touch_pad_t;

// Touch pad channel mapping (defined in touch.c)
extern const touch_pad_t TOUCH_PADS[MAX_TOUCH_PADS];

// Forward declaration for touchwheel instance (defined in touchwheel.h)
// Using incomplete type for function parameters
struct touchwheel_instance;

// Initialize touch sensor with calibration
void touch_init(bool enable_logging);

// Force recalibration (calls touch_calibrate with force=true)
void force_touch_calibration(void);

// Enable debug logging of touch events
void touch_enable_debug_logging(void);

// Check if logging is enabled (for conditional logging in related modules)
bool touch_is_logging_enabled(void);

// Query current pressed state
bool touch_is_pad_pressed(int pad_index);
const bool *touch_get_pressed_states(void);

// Stuck touch timeout configuration (how long a pad can be held before forcing release)
// Set to 0 to disable stuck touch detection
uint32_t touch_get_stuck_timeout_ms(void);
void touch_set_stuck_timeout_ms(uint32_t timeout_ms);

// Idle calibration interval configuration (how long device can be idle before recalibrating)
// This prevents drift from accumulating during extended idle periods.
// Set to 0 to disable proactive idle calibration.
// Default: 15 minutes (900000 ms)
uint32_t touch_get_idle_calibration_interval_ms(void);
void touch_set_idle_calibration_interval_ms(uint32_t interval_ms);

// Get timestamps for debugging/monitoring
uint32_t touch_get_last_calibration_time_ms(void);
uint32_t touch_get_last_touch_time_ms(void);

// Query detailed information for a specific pad
void touch_query_pad(int pad_index);

// Synchronize software state with hardware (used after reconfig operations)
void touch_sync_states_after_reconfig(void);

// Reset stuck touch pads by resetting benchmarks
void touch_reset_stuck_pads(void);

// Hold action suppression - when a hold action is active on a pad,
// suppress health check interventions (benchmark corruption, stuck touch)
// to allow intentional long presses without triggering recovery
void touch_set_hold_active(int pad_index, bool active);
bool touch_is_hold_active(int pad_index);
bool touch_is_any_hold_active(void);

// Clear pressed state for a specific pad (used during recovery)
void touch_clear_pressed_state(int pad_index);

// Manual calibration function (exposed for external calls)
esp_err_t touch_calibrate(bool force);

// Check for sensor drift
esp_err_t touch_check_drift(void);

// Touchwheel instance registration (for routing pad 0-7 events)
// Note: touchwheel_instance_t is defined in touchwheel.h
esp_err_t touch_register_touchwheel_instance(struct touchwheel_instance* instance);
esp_err_t touch_unregister_touchwheel_instance(struct touchwheel_instance* instance);

// Internal accessor functions (exposed for touchwheel_analog.c)
touch_channel_handle_t touch_get_channel_handle(int pad_index);

// Get the hardware channel number for a logical pad index
touch_pad_t touch_get_channel_for_pad(int pad_index);

#endif // TOUCH_H_
