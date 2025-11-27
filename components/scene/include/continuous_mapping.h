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

// Output type for continuous inputs
typedef enum {
  OUTPUT_TYPE_CC = 0,       // Send MIDI CC messages
  OUTPUT_TYPE_NOTE          // Send MIDI Note On/Off messages
} output_type_t;

// Maximum number of CCs that can be controlled simultaneously
#define MAX_MULTI_CC 4

// Continuous input mapping configuration
typedef struct {
  bool enabled;              // Whether this input is active
  output_type_t output_type; // CC or Note output
  
  // CC output parameters
  uint8_t cc_number;         // Primary MIDI CC (for backward compatibility)
  uint8_t cc_numbers[MAX_MULTI_CC];  // Additional CCs to send simultaneously
  uint8_t num_cc_numbers;    // Number of CCs in cc_numbers array (0 = use cc_number only)
  
  // Note output parameters
  uint8_t base_note;         // Base MIDI note (typically 60 = middle C)
  uint8_t note_range;        // Range in semitones (e.g., 24 = 2 octaves)
  uint8_t velocity;          // Note velocity (0-127)
  
  // Value transformation
  curve_t curve;             // Curve to apply before polarity
  polarity_t polarity;       // Polarity mode
  
  // Output scaling
  uint8_t min_value;         // Minimum output value (default 0)
  uint8_t max_value;         // Maximum output value (default 127)
  
  // Special behaviors (for proximity sensor)
  bool use_idle_value;       // Return to idle when no activity
  uint8_t idle_value;        // Value when idle (64 for CC bipolar, 60 for NOTE)
  uint16_t idle_timeout_ms;  // Time before reverting to idle
  
  // State tracking
  uint32_t last_activity_ms; // Last time value changed (for idle timeout)
  uint8_t last_value;        // Last sent value (CC or note number)
  bool note_active;          // For NOTE output: whether a note is currently on
} continuous_mapping_t;

// Apply polarity transformation to value
// input: 0-127 (from curve)
// polarity: transformation mode
// returns: scaled value
uint8_t apply_polarity(uint8_t input, polarity_t polarity);

// Process continuous input through mapping
// raw_input: 0-127 from sensor
// mapping: configuration
// returns: final output value (CC value 0-127 or note number 0-127)
uint8_t continuous_mapping_process(uint8_t raw_input, continuous_mapping_t* mapping);

// Convert 0-127 value to MIDI note number based on mapping
// value: 0-127 input value (after curve/polarity/scaling)
// mapping: configuration with base_note and note_range
// returns: MIDI note number (0-127)
uint8_t continuous_mapping_value_to_note(uint8_t value, const continuous_mapping_t* mapping);

// Check if mapping should revert to idle value
bool continuous_mapping_check_idle(continuous_mapping_t* mapping);

// Create default continuous mapping
continuous_mapping_t continuous_mapping_create(uint8_t cc_number);

// Send CC value to all configured CCs in mapping
// Uses multi-CC mode if num_cc_numbers > 0, otherwise uses cc_number
void continuous_mapping_send_cc(const continuous_mapping_t* mapping, uint8_t channel, uint8_t value);

#endif // CONTINUOUS_MAPPING_H

