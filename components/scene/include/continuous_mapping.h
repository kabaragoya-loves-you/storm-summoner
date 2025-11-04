#ifndef CONTINUOUS_MAPPING_H
#define CONTINUOUS_MAPPING_H

#include <stdint.h>
#include <stdbool.h>
#include "curve.h"

// Polarity modes
typedef enum {
  POLARITY_UNIPOLAR = 0,    // 0-max (normal)
  POLARITY_BIPOLAR,         // -max to +max (centered at 64)
  POLARITY_INVERTED         // max-0 (reversed)
} polarity_t;

// Continuous input mapping configuration
typedef struct {
  bool enabled;              // Whether this input is active
  uint8_t cc_number;         // MIDI CC to send
  
  // Value transformation
  curve_t curve;             // Curve to apply before polarity
  polarity_t polarity;       // Polarity mode
  
  // Output scaling
  uint8_t min_value;         // Minimum output value (default 0)
  uint8_t max_value;         // Maximum output value (default 127)
  
  // Special behaviors (for proximity sensor)
  bool use_idle_value;       // Return to idle when no activity
  uint8_t idle_value;        // Value when idle (typically 64 for bipolar)
  uint16_t idle_timeout_ms;  // Time before reverting to idle
  
  // State tracking
  uint32_t last_activity_ms; // Last time value changed (for idle timeout)
  uint8_t last_value;        // Last sent value
} continuous_mapping_t;

// Apply polarity transformation to value
// input: 0-127 (from curve)
// polarity: transformation mode
// returns: scaled value
uint8_t apply_polarity(uint8_t input, polarity_t polarity);

// Process continuous input through mapping
// raw_input: 0-127 from sensor
// mapping: configuration
// returns: final MIDI CC value (0-127)
uint8_t continuous_mapping_process(uint8_t raw_input, continuous_mapping_t* mapping);

// Check if mapping should revert to idle value
bool continuous_mapping_check_idle(continuous_mapping_t* mapping);

// Create default continuous mapping
continuous_mapping_t continuous_mapping_create(uint8_t cc_number);

#endif // CONTINUOUS_MAPPING_H

