#ifndef TOUCH_H_
#define TOUCH_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define MAX_TOUCH_PADS 13

typedef int touch_pad_t;

// Initialize touch sensor with calibration
void touch_init(bool enable_logging);

// Force recalibration (calls touch_calibrate with force=true)
void force_touch_calibration(void);

// Enable debug logging of touch events
void touch_enable_debug_logging(void);

// Reset stuck touch pads by resetting benchmarks
void touch_reset_stuck_pads(void);

// Manual calibration function (exposed for external calls)
esp_err_t touch_calibrate(bool force);

// Check for sensor drift
esp_err_t touch_check_drift(void);

#endif // TOUCH_H_
