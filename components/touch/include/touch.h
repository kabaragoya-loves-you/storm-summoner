#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/touch_sens.h"

#define MAX_TOUCH_PADS 13

typedef int touch_pad_t;

// Forward declaration for touchwheel instance (defined in touchwheel.h)
// Using incomplete type for function parameters
struct touchwheel_instance;

// Initialize touch sensor with calibration
void touch_init(bool enable_logging);

// Force recalibration (calls touch_calibrate with force=true)
void force_touch_calibration(void);

// Enable debug logging of touch events
void touch_enable_debug_logging(void);

// Query current pressed state
bool touch_is_pad_pressed(int pad_index);
const bool *touch_get_pressed_states(void);

// Query detailed information for a specific pad
void touch_query_pad(int pad_index);

// Synchronize software state with hardware (used after reconfig operations)
void touch_sync_states_after_reconfig(void);

// Reset stuck touch pads by resetting benchmarks
void touch_reset_stuck_pads(void);

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

#endif // TOUCH_H_
