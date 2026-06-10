#ifndef SAMPLE_HOLD_H
#define SAMPLE_HOLD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "lfo.h"

// S+H mode (time-based vs triggered)
typedef enum {
  SAMPLE_HOLD_MODE_CONTINUOUS = 0,
  SAMPLE_HOLD_MODE_STEP
} sample_hold_mode_t;

// S+H rate mode (Hz-based vs tempo-synced)
typedef enum {
  SAMPLE_HOLD_RATE_MODE_FREE = 0,  // Use rate_hz_x100
  SAMPLE_HOLD_RATE_MODE_SYNC       // Sync to BPM with multiplier
} sample_hold_rate_mode_t;

// S+H start mode (when scene loads)
typedef enum {
  SAMPLE_HOLD_START_RUNNING = 0,   // Start immediately when scene loads
  SAMPLE_HOLD_START_PAUSED,        // Start paused (requires action to start)
  SAMPLE_HOLD_START_TRANSPORT      // Follow transport state (play/stop)
} sample_hold_start_mode_t;

// S+H configuration (stored per-scene)
typedef struct {
  bool enabled;
  sample_hold_mode_t mode;
  sample_hold_rate_mode_t rate_mode;
  sample_hold_start_mode_t start_mode;
  uint16_t rate_hz_x100;        // 50-2500 (0.5-25.0 Hz), used when rate_mode == FREE
  uint16_t sync_mult_x1000;     // Legacy; migrated to division on load
  lfo_note_division_t division; // Note division when rate_mode == SYNC
  bool glide;                   // Enable smooth transitions between values
  uint8_t probability;          // Chance of firing 10-100% (default 100)
  uint8_t pattern_length;       // Step pattern length 2-8 (0 = disabled)
  uint8_t pattern_mask;         // Bitmask of active steps (bit 0 = step 1)
} sample_hold_config_t;

// Initialize the S+H component
esp_err_t sample_hold_init(void);

// Start/stop S+H processing
void sample_hold_start(void);
void sample_hold_stop(void);

// Apply configuration from scene
void sample_hold_apply_config(const sample_hold_config_t* config);

// Get current configuration
void sample_hold_get_config(sample_hold_config_t* config);

// Create default configuration
sample_hold_config_t sample_hold_config_create_default(void);

// Manual step trigger (for Step mode and ACTION_SAMPLE_HOLD + VARIANT_STEP)
void sample_hold_step(void);

// Enable/disable S+H (config setting)
void sample_hold_set_enabled(bool enabled);
bool sample_hold_is_enabled(void);

// Check if S+H is currently running
bool sample_hold_is_running(void);

// Get current held value (0-127)
uint8_t sample_hold_get_value(void);

// Configuration setters
void sample_hold_set_mode(sample_hold_mode_t mode);
sample_hold_mode_t sample_hold_get_mode(void);

void sample_hold_set_rate_mode(sample_hold_rate_mode_t rate_mode);
sample_hold_rate_mode_t sample_hold_get_rate_mode(void);

void sample_hold_set_start_mode(sample_hold_start_mode_t start_mode);
sample_hold_start_mode_t sample_hold_get_start_mode(void);

void sample_hold_set_rate_hz(float rate_hz);
float sample_hold_get_rate_hz(void);

void sample_hold_set_sync_mult(float mult);
float sample_hold_get_sync_mult(void);

void sample_hold_set_division(lfo_note_division_t division);
lfo_note_division_t sample_hold_get_division(void);

void sample_hold_set_glide(bool glide);
bool sample_hold_get_glide(void);

void sample_hold_set_probability(uint8_t probability);
uint8_t sample_hold_get_probability(void);

void sample_hold_set_pattern_length(uint8_t length);
uint8_t sample_hold_get_pattern_length(void);

void sample_hold_set_pattern_mask(uint8_t mask);
uint8_t sample_hold_get_pattern_mask(void);

// String conversion utilities
const char* sample_hold_mode_to_string(sample_hold_mode_t mode);
sample_hold_mode_t sample_hold_mode_from_string(const char* str);

const char* sample_hold_rate_mode_to_string(sample_hold_rate_mode_t mode);
sample_hold_rate_mode_t sample_hold_rate_mode_from_string(const char* str);

const char* sample_hold_start_mode_to_string(sample_hold_start_mode_t mode);
sample_hold_start_mode_t sample_hold_start_mode_from_string(const char* str);

// Apply start mode (called when scene loads, after sample_hold_apply_config)
void sample_hold_apply_start_mode(void);

// Toggle S+H enabled state (for S+H Toggle action)
void sample_hold_toggle(void);

// Dynamic rate modulation (for LFO -> S+H rate)
// lfo_value 0-127 maps to discrete rate options based on current rate_mode
void sample_hold_set_dynamic_rate(uint8_t lfo_value);
uint8_t sample_hold_get_dynamic_rate(void);
bool sample_hold_has_dynamic_rate(void);
void sample_hold_clear_dynamic_rate(void);

#endif // SAMPLE_HOLD_H
