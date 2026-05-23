#ifndef LFO_H
#define LFO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "curve.h"

// Number of LFO slots
#define LFO_NUM_SLOTS 2

// LFO waveform types
typedef enum {
  LFO_WAVEFORM_SINE = 0,
  LFO_WAVEFORM_TRIANGLE,
  LFO_WAVEFORM_SQUARE,
  LFO_WAVEFORM_SAW_UP,
  LFO_WAVEFORM_SAW_DOWN,
  LFO_WAVEFORM_SAMPLE_HOLD,
  LFO_WAVEFORM_CUSTOM,
  LFO_WAVEFORM_MAX
} lfo_waveform_t;

// LFO rate mode
typedef enum {
  LFO_RATE_MODE_FREE = 0,   // Hz-based (0.05 - 20.0 Hz)
  LFO_RATE_MODE_TEMPO,      // Note division sync
  LFO_RATE_MODE_TOUCHWHEEL, // Rate controlled by touchwheel position
  LFO_RATE_MODE_EXPRESSION, // Rate controlled by expression pedal
  LFO_RATE_MODE_CV,         // Rate controlled by CV input
  LFO_RATE_MODE_ALS,        // Rate controlled by ambient light sensor
  LFO_RATE_MODE_PROXIMITY   // Rate controlled by proximity sensor
} lfo_rate_mode_t;

// LFO start mode (when scene loads)
typedef enum {
  LFO_START_RUNNING = 0,    // Start immediately when scene loads
  LFO_START_PAUSED,         // Start paused (requires action to start)
  LFO_START_TRANSPORT       // Follow transport state (play/stop)
} lfo_start_mode_t;

// LFO trigger timing (when LFO Start action is triggered)
typedef enum {
  LFO_TRIGGER_IMMEDIATE = 0,  // Start immediately (default)
  LFO_TRIGGER_NEXT_BEAT,      // Wait for next beat
  LFO_TRIGGER_NEXT_BAR        // Wait for next bar
} lfo_trigger_timing_t;

// Note divisions for tempo sync mode
typedef enum {
  LFO_DIVISION_16_BARS = 0, // 16 bars per cycle
  LFO_DIVISION_12_BARS,     // 12 bars per cycle
  LFO_DIVISION_8_BARS,      // 8 bars per cycle
  LFO_DIVISION_4_BARS,      // 4 bars per cycle
  LFO_DIVISION_2_BARS,      // 2 bars per cycle
  LFO_DIVISION_1_BAR,       // 1 bar per cycle
  LFO_DIVISION_HALF,        // 1/2 note
  LFO_DIVISION_QUARTER,     // 1/4 note
  LFO_DIVISION_EIGHTH,      // 1/8 note
  LFO_DIVISION_SIXTEENTH,   // 1/16 note
  LFO_DIVISION_32ND,        // 1/32 note
  LFO_DIVISION_MAX
} lfo_note_division_t;

// LFO resolution mode (controls MIDI output density)
typedef enum {
  LFO_RESOLUTION_AUTO = 0,  // Smart: 30Hz for slow, 32 steps for fast (default)
  LFO_RESOLUTION_COARSE,    // 16 steps per cycle
  LFO_RESOLUTION_MEDIUM,    // 32 steps per cycle
  LFO_RESOLUTION_FINE,      // 64 steps per cycle
  LFO_RESOLUTION_MANUAL     // User-specified steps (16/32/64/128)
} lfo_resolution_mode_t;

// LFO configuration (stored per-scene)
typedef struct {
  bool enabled;
  lfo_waveform_t waveform;
  lfo_rate_mode_t rate_mode;
  lfo_start_mode_t start_mode;
  lfo_trigger_timing_t trigger_timing;  // When to start after LFO Start action
  bool repeat;  // true = loop infinitely (default), false = one-shot (run once)
  bool reset_phase;  // true = restart from beginning, false = continue from previous position
  bool restore_on_stop;  // true = send phase-0 value when LFO stops

  // Free mode rate (stored as fixed point: rate_hz_x100 / 100.0)
  // Range: 5-2000 (0.05 - 20.0 Hz)
  uint16_t rate_hz_x100;

  // Tempo sync rate
  lfo_note_division_t division;

  // Phase offset (0-255 = 0-360 degrees)
  uint8_t phase_offset;

  // Square wave duty cycle (0-127 = 0-100%)
  uint8_t duty_cycle;

  // Output range (floor/ceiling) - applied at full resolution before MIDI conversion
  uint8_t floor;    // Minimum output value (0-127, default 0)
  uint8_t ceiling;  // Maximum output value (0-127, default 127)

  // Resolution mode (controls MIDI output density)
  lfo_resolution_mode_t resolution_mode;  // Default: AUTO
  uint8_t manual_steps;  // 16, 32, 64, or 128 (only used when MANUAL)

  // Custom curve pointer (for CUSTOM waveform)
  custom_curve_t* custom_curve;
} lfo_config_t;

// Initialize the LFO component
esp_err_t lfo_init(void);

// Start/stop LFO processing
void lfo_start(void);
void lfo_stop(void);

// Enable/disable individual LFOs
void lfo_enable(uint8_t slot, bool enabled);
bool lfo_is_enabled(uint8_t slot);

// Configuration getters/setters
void lfo_set_waveform(uint8_t slot, lfo_waveform_t waveform);
lfo_waveform_t lfo_get_waveform(uint8_t slot);

void lfo_set_rate_mode(uint8_t slot, lfo_rate_mode_t mode);
lfo_rate_mode_t lfo_get_rate_mode(uint8_t slot);

void lfo_set_start_mode(uint8_t slot, lfo_start_mode_t mode);
lfo_start_mode_t lfo_get_start_mode(uint8_t slot);

void lfo_set_trigger_timing(uint8_t slot, lfo_trigger_timing_t timing);
lfo_trigger_timing_t lfo_get_trigger_timing(uint8_t slot);

void lfo_set_repeat(uint8_t slot, bool repeat);
bool lfo_get_repeat(uint8_t slot);

void lfo_set_reset_phase(uint8_t slot, bool reset);
bool lfo_get_reset_phase(uint8_t slot);

void lfo_set_restore_on_stop(uint8_t slot, bool restore);
bool lfo_get_restore_on_stop(uint8_t slot);

void lfo_set_rate_hz(uint8_t slot, float rate_hz);
float lfo_get_rate_hz(uint8_t slot);

void lfo_set_division(uint8_t slot, lfo_note_division_t division);
lfo_note_division_t lfo_get_division(uint8_t slot);

void lfo_set_phase_offset(uint8_t slot, uint8_t offset);
uint8_t lfo_get_phase_offset(uint8_t slot);

void lfo_set_duty_cycle(uint8_t slot, uint8_t duty);
uint8_t lfo_get_duty_cycle(uint8_t slot);

void lfo_set_floor(uint8_t slot, uint8_t floor);
uint8_t lfo_get_floor(uint8_t slot);

void lfo_set_ceiling(uint8_t slot, uint8_t ceiling);
uint8_t lfo_get_ceiling(uint8_t slot);

void lfo_set_custom_curve(uint8_t slot, custom_curve_t* curve);
custom_curve_t* lfo_get_custom_curve(uint8_t slot);

void lfo_set_resolution_mode(uint8_t slot, lfo_resolution_mode_t mode);
lfo_resolution_mode_t lfo_get_resolution_mode(uint8_t slot);

void lfo_set_manual_steps(uint8_t slot, uint8_t steps);
uint8_t lfo_get_manual_steps(uint8_t slot);

// Reset phase to 0
void lfo_reset_phase(uint8_t slot);

// Get current LFO value (0-127)
uint8_t lfo_get_value(uint8_t slot);

// Get current phase (0-255)
uint8_t lfo_get_phase(uint8_t slot);

// Get waveform value at specified phase (0-255 = 0-360 degrees)
// Useful for restore_on_stop: pass phase=0 to get starting value
uint8_t lfo_get_value_at_phase(uint8_t slot, uint8_t phase);

// Apply a full config struct
void lfo_apply_config(uint8_t slot, const lfo_config_t* config);
void lfo_get_config(uint8_t slot, lfo_config_t* config);

// Create default config
lfo_config_t lfo_config_create_default(void);

// String conversion utilities
const char* lfo_waveform_to_string(lfo_waveform_t waveform);
const char* lfo_division_to_string(lfo_note_division_t division);
const char* lfo_start_mode_to_string(lfo_start_mode_t mode);
const char* lfo_trigger_timing_to_string(lfo_trigger_timing_t timing);
const char* lfo_rate_mode_to_string(lfo_rate_mode_t mode);
lfo_waveform_t lfo_waveform_from_string(const char* str);
lfo_note_division_t lfo_division_from_string(const char* str);
lfo_start_mode_t lfo_start_mode_from_string(const char* str);
lfo_trigger_timing_t lfo_trigger_timing_from_string(const char* str);
lfo_rate_mode_t lfo_rate_mode_from_string(const char* str);
const char* lfo_resolution_mode_to_string(lfo_resolution_mode_t mode);
lfo_resolution_mode_t lfo_resolution_mode_from_string(const char* str);

// Apply start mode settings (called on scene load)
void lfo_apply_start_modes(void);

// Apply start mode settings for a single slot (called when a slot is newly
// enabled mid-scene, e.g. after exiting programming mode).
void lfo_apply_start_mode_one(uint8_t slot);

// Trigger LFO start (respects trigger_timing setting)
// Returns true if started immediately, false if pending
bool lfo_trigger_start(uint8_t slot);

// Queue an additional cycle for one-shot LFO
void lfo_queue_cycle(uint8_t slot);

// Check if LFO has completed its one-shot cycle
bool lfo_is_cycle_completed(uint8_t slot);

// Check if LFO has a pending start
bool lfo_is_pending_start(uint8_t slot);

// Dynamic modulation (from external sources like other LFO, expression, etc.)
// These override/modulate the static floor/ceiling and rate settings

// Set dynamic depth modulation (0-127 = 0-100% of configured range)
// depth_value=0: flat line at center, depth_value=127: full configured range
void lfo_set_dynamic_depth(uint8_t slot, uint8_t depth_value);
uint8_t lfo_get_dynamic_depth(uint8_t slot);

// Set dynamic rate modulation (0-127 = 0.1-10.0 Hz exponential)
// Used when external source controls rate
void lfo_set_dynamic_rate(uint8_t slot, uint8_t rate_value);
uint8_t lfo_get_dynamic_rate(uint8_t slot);

// Get effective floor/ceiling after applying dynamic depth
uint8_t lfo_get_effective_floor(uint8_t slot);
uint8_t lfo_get_effective_ceiling(uint8_t slot);

// Check if dynamic modulation is active
bool lfo_has_dynamic_depth(uint8_t slot);
bool lfo_has_dynamic_rate(uint8_t slot);

// Clear dynamic modulation (returns to static config values)
void lfo_clear_dynamic_depth(uint8_t slot);
void lfo_clear_dynamic_rate(uint8_t slot);

#endif // LFO_H
