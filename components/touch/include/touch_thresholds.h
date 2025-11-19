#ifndef TOUCH_THRESHOLDS_H_
#define TOUCH_THRESHOLDS_H_

#include "esp_err.h"
#include "touch.h"  // For touch_pad_t and MAX_TOUCH_PADS
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t baseline;     // Average reading when not touched
  uint32_t threshold;    // Calculated threshold value
  uint32_t variance;     // Variance in readings (for noise assessment)
  bool valid;           // Whether this pad's calibration is valid
} touch_pad_calibration_t;

typedef enum {
  TOUCH_CALIBRATION_REASON_NONE = 0,
  TOUCH_CALIBRATION_REASON_DRIFT,
  TOUCH_CALIBRATION_REASON_BENCHMARK_CORRUPTION,
  TOUCH_CALIBRATION_REASON_MANUAL,
} touch_calibration_reason_t;

void touch_thresholds_init(void);

esp_err_t touch_calibrate(bool force);

esp_err_t touch_check_drift(void);

esp_err_t touch_get_calibration_data(touch_pad_t pad_num, touch_pad_calibration_t *data);

void touch_display_calibration_data(void);

// Update thresholds based on current benchmark values
esp_err_t touch_update_thresholds_from_benchmarks(void);

void touch_thresholds_request_calibration(touch_calibration_reason_t reason, bool force);

#endif // TOUCH_THRESHOLDS_H_

