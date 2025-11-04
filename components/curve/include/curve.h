#ifndef CURVE_H
#define CURVE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Curve types
typedef enum {
  CURVE_LINEAR = 0,         // Straight line (y = x)
  CURVE_EXPONENTIAL,        // Exponential rise
  CURVE_LOGARITHMIC,        // Logarithmic rise
  CURVE_S_CURVE,            // S-shaped (sigmoid)
  CURVE_INVERSE_S,          // Inverse S-curve
  CURVE_QUADRATIC,          // x^2
  CURVE_SQUARE_ROOT,        // sqrt(x)
  CURVE_SINE,               // Sine wave (0 to 90 degrees)
  CURVE_CUSTOM,             // User-recorded curve
  CURVE_MAX
} curve_type_t;

// Curve slope/intensity (for curves that support it)
typedef enum {
  CURVE_SLOPE_GENTLE = 0,   // Subtle curve
  CURVE_SLOPE_MEDIUM,       // Moderate curve
  CURVE_SLOPE_STEEP         // Aggressive curve
} curve_slope_t;

// Custom curve (127 values, one per input step)
#define CURVE_RESOLUTION 128

typedef struct {
  uint8_t values[CURVE_RESOLUTION];  // Lookup table
  bool valid;                        // Whether curve has been recorded
} custom_curve_t;

// Curve definition
typedef struct {
  curve_type_t type;
  curve_slope_t slope;           // Intensity for curves that support it
  custom_curve_t* custom_data;   // Pointer to custom curve (NULL if not custom)
} curve_t;

// Initialize curve component
esp_err_t curve_init(void);

// Apply curve transformation to input value
// input: 0-127
// returns: 0-127 (transformed value)
uint8_t curve_apply(const curve_t* curve, uint8_t input);

// Create a curve with default settings
curve_t curve_create(curve_type_t type);
curve_t curve_create_with_slope(curve_type_t type, curve_slope_t slope);

// Custom curve recording
typedef struct {
  uint8_t samples[256];      // Raw samples (more than needed for oversampling)
  uint16_t sample_count;
  bool recording;
  uint32_t start_time_ms;
} curve_recorder_t;

// Start recording a custom curve
esp_err_t curve_start_recording(curve_recorder_t* recorder);

// Add sample during recording
esp_err_t curve_add_sample(curve_recorder_t* recorder, uint8_t value);

// Finish recording and generate custom curve
esp_err_t curve_finish_recording(curve_recorder_t* recorder, custom_curve_t* output);

// Cancel recording
void curve_cancel_recording(curve_recorder_t* recorder);

// Get curve type name (for debugging/UI)
const char* curve_type_to_string(curve_type_t type);

// Free custom curve data
void curve_free_custom(custom_curve_t* custom);

#endif // CURVE_H

