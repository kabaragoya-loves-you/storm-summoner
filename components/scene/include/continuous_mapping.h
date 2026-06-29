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
  OUTPUT_TYPE_NOTE,         // Send MIDI Note On/Off messages
  OUTPUT_TYPE_LFO_RATE,     // Modulate LFO rate (uses lfo_target)
  OUTPUT_TYPE_LFO_DEPTH,    // Modulate LFO depth (uses lfo_target)
  OUTPUT_TYPE_LFO2_RATE,    // LFO1 -> LFO2 rate (cross-modulation)
  OUTPUT_TYPE_LFO2_DEPTH,   // LFO1 -> LFO2 depth (cross-modulation)
  OUTPUT_TYPE_LFO1_RATE,    // LFO2 -> LFO1 rate (cross-modulation)
  OUTPUT_TYPE_LFO1_DEPTH,   // LFO2 -> LFO1 depth (cross-modulation)
  OUTPUT_TYPE_RTG_RATE,     // Modulate RTG rate
  OUTPUT_TYPE_SH_RATE,      // Modulate Sample+Hold rate
  OUTPUT_TYPE_PITCH_BEND,   // Send pitch bend (uses note_channel)
  OUTPUT_TYPE_TEMPO_NUDGE   // Nudge scene BPM around scene->bpm (bipolar, centered)
} output_type_t;

// LFO target for LFO_RATE/LFO_DEPTH output types
typedef enum {
  LFO_TARGET_LFO1 = 0,      // Target LFO1 only
  LFO_TARGET_LFO2,          // Target LFO2 only
  LFO_TARGET_BOTH           // Target both LFOs
} lfo_target_t;

// Polyphony mode for note output
typedef enum {
  POLYPHONY_MONO = 0,       // Single voice (default)
  POLYPHONY_POLY            // Up to 4 voices
} polyphony_mode_t;

// Maximum polyphonic voices
#define MAX_POLY_VOICES 4

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
  bool note_latch;           // If true, notes hold until manually released
  uint16_t note_release_ms;  // Release delay in ms (100-4000, only used when latch is on)
  polyphony_mode_t polyphony; // Mono (1 voice) or Poly (up to 4 voices)
  
  // Value transformation
  curve_t curve;             // Curve to apply before polarity
  polarity_t polarity;       // Polarity mode
  
  // Output scaling: raw 0 -> min_value, raw 64 (rest) -> middle_value,
  // raw 127 -> max_value. Piecewise-linear interpolation between anchors
  // lets callers shift the "rest" output independently of the extents.
  // With middle_value == 64 and min=0/max=127, behavior is identical to the
  // classic linear 0..127 mapping.
  uint8_t min_value;         // Minimum output value (default 0)
  uint8_t middle_value;      // Output at raw=64 / sensor rest (default 64)
  uint8_t max_value;         // Maximum output value (default 127)
  
  // Special behaviors (for proximity sensor)
  bool use_idle_value;       // Return to idle when no activity
  uint8_t idle_value;        // Value when idle (64 for CC bipolar, 60 for NOTE)
  uint16_t idle_timeout_ms;  // Time before reverting to idle
  
  // LFO modulation target (for OUTPUT_TYPE_LFO_RATE/LFO_DEPTH)
  lfo_target_t lfo_target;   // Which LFO(s) to target
  
  // State tracking
  uint32_t last_activity_ms; // Last time value changed (for idle timeout)
  uint8_t last_value;        // Last sent CC value (0-127)
  uint8_t last_note;         // Last sent note number (for NOTE output)
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

// Proximity-style unipolar bipolar: nothing in field -> middle, far in-range ->
// min, close -> max. Curve and min/middle/max scaling apply. Updates
// mapping->last_value and last_activity_ms like continuous_mapping_process().
uint8_t continuous_mapping_unipolar_bipolar_map(uint8_t raw_input,
  continuous_mapping_t* mapping);

// Apply curve/polarity/scaling for the mapping's polarity mode; updates
// mapping->last_value. Returns 0 if mapping is NULL or disabled.
uint8_t continuous_mapping_apply(uint8_t raw_input, continuous_mapping_t* mapping);

// Velocity sample for CV/Gate takeover (ignores enabled; updates last_value).
uint8_t continuous_mapping_velocity_sample(uint8_t raw_input, continuous_mapping_t* mapping);

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

