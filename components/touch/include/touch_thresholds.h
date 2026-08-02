#ifndef TOUCH_THRESHOLDS_H_
#define TOUCH_THRESHOLDS_H_

#include "esp_err.h"
#include "touch.h"  // For touch_pad_t and MAX_TOUCH_PADS
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_PAD12_INDEX 12

typedef struct {
  uint32_t baseline;     // Average reading when not touched
  uint32_t threshold;    // Calculated threshold value
  uint32_t variance;     // Variance in readings (for noise assessment)
  bool valid;           // Whether this pad's calibration is valid
} touch_pad_calibration_t;

typedef struct {
  bool valid;
  uint32_t baseline;     // Measured idle resting value
  uint32_t touch_elev;   // Median peak elevation (smooth - idle) from guided holds
} touch_screw_calibration_t;

typedef enum {
  TOUCH_CALIBRATION_REASON_NONE = 0,
  TOUCH_CALIBRATION_REASON_DRIFT,
  TOUCH_CALIBRATION_REASON_BENCHMARK_CORRUPTION,
  TOUCH_CALIBRATION_REASON_MANUAL,
  TOUCH_CALIBRATION_REASON_IDLE,  // Proactive calibration during idle period
} touch_calibration_reason_t;

void touch_thresholds_init(void);

esp_err_t touch_calibrate(bool force);

esp_err_t touch_check_drift(void);

esp_err_t touch_get_calibration_data(touch_pad_t pad_num, touch_pad_calibration_t *data);

void touch_display_calibration_data(void);

// Update thresholds based on current benchmark values
esp_err_t touch_update_thresholds_from_benchmarks(void);

// Calibrate a single pad with thread safety (used by health check)
esp_err_t touch_calibrate_pad(int pad_index);

// Fast recovery for a single pad (resets benchmark, updates baseline/threshold instantly)
esp_err_t touch_recover_pad_state(int pad_index);

void touch_thresholds_request_calibration(touch_calibration_reason_t reason, bool force);

// Process any pending calibration requests (called from health check task)
// Returns true if a calibration was processed
bool touch_thresholds_process_pending(void);

// If idle calib refreshed RAM without persisting, flush those values to NVS now.
// Call from intentional, non-clock-critical moments (programming enter, scene save).
void touch_thresholds_flush_nvs_if_dirty(void);

// --- Screw (pad 12) calibration ---
bool touch_screw_calib_is_valid(void);
esp_err_t touch_screw_calib_get(touch_screw_calibration_t* out);
// Persist measured screw metrics, apply to pad 12, and update hardware threshold.
esp_err_t touch_screw_calib_apply(uint32_t baseline, uint32_t touch_elev);
// Elevation bar used by phantom/long-press guards for pad 12.
// When screw calib is valid: touch_elev/2. Otherwise: stored threshold/2.
uint32_t touch_pad12_elev_thresh(void);
// Re-apply screw-derived pad-12 baseline/threshold after a full-pad calibrate.
void touch_screw_calib_reapply_if_valid(void);

#endif // TOUCH_THRESHOLDS_H_
