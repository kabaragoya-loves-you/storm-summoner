#ifndef _INPUT_MODE_H
#define _INPUT_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Main input modes (what the user selects)
typedef enum {
  INPUT_MODE_CV = 0,        // Control Voltage mode
  INPUT_MODE_CLOCK_SYNC,    // Clock sync detection mode
  INPUT_MODE_AUDIO          // Audio analysis mode (future)
} input_mode_t;

// CV voltage ranges (only relevant when INPUT_MODE_CV is selected)
// Note: Some ranges share switch channels but use different DAC reference voltages
typedef enum {
  CV_RANGE_BIPOLAR_10V = 0,  // ±10V (switch ch 0)
  CV_RANGE_10V = 1,          // 0-10V (switch ch 1)
  CV_RANGE_BIPOLAR_5V = 2,   // ±5V (switch ch 1, different DAC voltage)
  CV_RANGE_5V = 3,           // 0-5V (switch ch 2)
  CV_RANGE_3V3 = 4           // 0-3.3V (switch ch 3)
} cv_range_t;

// CV processing modes (how to interpret the voltage)
typedef enum {
  CV_MODE_LINEAR = 0,       // Linear 0-127 MIDI mapping
  CV_MODE_PITCH,            // 1V/octave pitch CV
  CV_MODE_GATE              // Gate/trigger detection
} cv_mode_t;

/**
 * Set the main input mode
 * @param mode The input mode to use
 * @return ESP_OK on success
 */
esp_err_t input_set_mode(input_mode_t mode);

/**
 * Get the current input mode
 * @return Current input mode
 */
input_mode_t input_get_mode(void);

/**
 * Check if a specific input mode is currently active
 * @param mode The mode to check
 * @return true if the mode is active
 */
bool input_is_mode_active(input_mode_t mode);

#endif /* _INPUT_MODE_H */
