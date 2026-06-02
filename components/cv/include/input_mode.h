#ifndef _INPUT_MODE_H
#define _INPUT_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Main input modes (what the user selects)
typedef enum {
  INPUT_MODE_NONE = -1,     // CV disabled for this scene
  INPUT_MODE_CV = 0,        // Control Voltage mode
  INPUT_MODE_CLOCK_SYNC,    // Clock sync detection mode
  INPUT_MODE_AUDIO,         // Audio analysis mode (future)
  INPUT_MODE_NOTE,          // CV pitch + Expression gate → MIDI notes
  INPUT_MODE_TRIGGER        // CV threshold → action trigger
} input_mode_t;

// Velocity mode for note output from continuous inputs
typedef enum {
  VELOCITY_MODE_FIXED = 0,        // Use fixed velocity value
  VELOCITY_MODE_GATE_VOLTAGE,     // Derive from gate voltage (expression jack ADC)
  VELOCITY_MODE_TOUCHWHEEL        // Derive from touchwheel position (when in velocity mode)
} velocity_mode_t;

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
  CV_MODE_PITCH             // Pitch CV (standard determined by cv_pitch_standard_t)
} cv_mode_t;

// CV pitch standards (for CV_MODE_PITCH)
typedef enum {
  CV_PITCH_1V_OCTAVE_C0 = 0,  // 1V/octave, C0 at 0V (MIDI note 12)
  CV_PITCH_1V_OCTAVE_C2,      // 1V/octave, C2 at 0V (MIDI note 36)
  CV_PITCH_HZ_V               // Hz/V (Buchla standard)
} cv_pitch_standard_t;

// Audio envelope follower polarity (for INPUT_MODE_AUDIO)
typedef enum {
  AUDIO_POLARITY_ATTRACT = 0,  // Louder = higher CC value
  AUDIO_POLARITY_REPEL = 1     // Louder = lower CC value (ducking)
} audio_polarity_t;

// Audio envelope follower configuration (for INPUT_MODE_AUDIO)
typedef struct audio_config_t {
  cv_range_t range;           // CV_RANGE_BIPOLAR_5V or CV_RANGE_BIPOLAR_10V
  uint8_t sensitivity;        // 0-255 maps to 0.25x - 64x gain (exponential curve)
  uint16_t attack_ms;         // 5-100ms
  uint16_t release_ms;        // 50-2000ms
  uint8_t threshold;          // 0-127 (0 = no threshold, signals below ignored)
  audio_polarity_t polarity;  // ATTRACT or REPEL
} audio_config_t;

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
