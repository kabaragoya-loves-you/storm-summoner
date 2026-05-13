#ifndef RTG_H
#define RTG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// RTG mode (time-based vs triggered)
typedef enum {
  RTG_MODE_CONTINUOUS = 0,
  RTG_MODE_STEP
} rtg_mode_t;

// RTG rate mode (Hz-based vs tempo-synced)
typedef enum {
  RTG_RATE_MODE_FREE = 0,  // Use rate_hz_x100
  RTG_RATE_MODE_SYNC       // Sync to BPM (one step per beat)
} rtg_rate_mode_t;

// RTG start mode (when scene loads)
typedef enum {
  RTG_START_RUNNING = 0,   // Start immediately when scene loads
  RTG_START_PAUSED,        // Start paused (requires action to start)
  RTG_START_TRANSPORT      // Follow transport state (play/stop)
} rtg_start_mode_t;

// Generator type: random tone (LFSR) or Shepard tone (octave-spaced voices)
typedef enum {
  RTG_GEN_RANDOM = 0,
  RTG_GEN_SHEPARD
} rtg_generator_t;

// Direction of Shepard motion
typedef enum {
  SHEPARD_DIR_RISING = 0,
  SHEPARD_DIR_FALLING
} shepard_direction_t;

// Voice layout for Shepard mode (only relevant when smooth/glide is on)
typedef enum {
  SHEPARD_LAYOUT_SINGLE = 0,  // All voices share note_channel
  SHEPARD_LAYOUT_MULTI        // Voice N -> (note_channel + N) % 16
} shepard_layout_t;

// Per-voice fade source for Shepard mode (only relevant when smooth is on)
typedef enum {
  SHEPARD_FADE_NONE = 0,      // Bell curve only at retrigger
  SHEPARD_FADE_CC11,          // Continuous CC11 expression per channel
  SHEPARD_FADE_POLY_AT        // Polyphonic aftertouch per voice
} shepard_fade_t;

// Smoothness style for Shepard mode (only relevant when smooth/glide is on)
typedef enum {
  SHEPARD_STYLE_STREAM = 0,   // Retrigger every semitone, stream bend within tick
  SHEPARD_STYLE_WIDE,         // Retrigger every K semitones, stream bend across ticks
  SHEPARD_STYLE_CROSSFADE     // Retrigger every semitone with overlap via fade source
} shepard_style_t;

// Maximum voice count for Shepard mode (8 octaves max)
#define RTG_SHEPARD_MAX_VOICES 8

// RTG configuration (stored per-scene)
typedef struct {
  bool enabled;
  rtg_mode_t mode;
  rtg_rate_mode_t rate_mode;    // FREE (Hz) or SYNC (BPM)
  rtg_start_mode_t start_mode;  // When to start RTG on scene load
  uint16_t rate_hz_x100;        // 50-2500 (0.5-25.0 Hz), used when rate_mode == FREE
  uint16_t sync_mult_x1000;     // 125-8000 (0.125x-8.0x), used when rate_mode == SYNC
  bool glide;                   // Random: pitch bend smoothing. Shepard: smooth (bend + fade)
  uint8_t velocity;             // Random: note velocity. Shepard: peak velocity (bell-scaled)
  uint8_t note_min;             // Range floor (default 36/C2). Shepard: BASE_NOTE
  uint8_t note_max;             // Range ceiling (default 96/C7). Shepard: derives octave count
  uint8_t probability;          // Chance of firing 10-100% (default 100)
  uint8_t pattern_length;       // Step pattern length 2-8 (0 = disabled)
  uint8_t pattern_mask;         // Bitmask of active steps (bit 0 = step 1)
  rtg_generator_t generator;    // Random or Shepard
  shepard_direction_t shepard_direction;  // Rising or Falling
  shepard_layout_t shepard_layout;        // Single channel or multi-channel
  shepard_fade_t shepard_fade;            // None / CC11 / Poly AT
  shepard_style_t shepard_style;          // Stream / Wide / Crossfade (smooth Shepard)
  uint8_t shepard_wide_semis;             // Retrigger spacing for Wide style (2..4)
} rtg_config_t;

// Initialize the RTG component
esp_err_t rtg_init(void);

// Start/stop RTG processing
void rtg_start(void);
void rtg_stop(void);

// Release any active notes (for mode transitions)
void rtg_release_notes(void);

// Apply configuration from scene
void rtg_apply_config(const rtg_config_t* config);

// Get current configuration
void rtg_get_config(rtg_config_t* config);

// Create default configuration
rtg_config_t rtg_config_create_default(void);

// Manual step trigger (for Step mode and ACTION_STEP)
void rtg_step(void);

// Tick function for continuous mode (called from main loop)
void rtg_tick(uint32_t now_ms);

// Enable/disable RTG (config setting)
void rtg_set_enabled(bool enabled);
bool rtg_is_enabled(void);

// Check if RTG is currently running (producing output)
bool rtg_is_running(void);

// Configuration setters
void rtg_set_mode(rtg_mode_t mode);
rtg_mode_t rtg_get_mode(void);

void rtg_set_rate_mode(rtg_rate_mode_t rate_mode);
rtg_rate_mode_t rtg_get_rate_mode(void);

void rtg_set_start_mode(rtg_start_mode_t start_mode);
rtg_start_mode_t rtg_get_start_mode(void);

void rtg_set_rate_hz(float rate_hz);
float rtg_get_rate_hz(void);

void rtg_set_sync_mult(float mult);
float rtg_get_sync_mult(void);

// Notify RTG that touchwheel rate modulation changed (triggers timer update)
void rtg_touchwheel_rate_changed(void);

// Dynamic rate modulation (for LFO -> RTG rate)
// lfo_value 0-127 maps to discrete rate options based on current rate_mode
void rtg_set_dynamic_rate(uint8_t lfo_value);
uint8_t rtg_get_dynamic_rate(void);
bool rtg_has_dynamic_rate(void);
void rtg_clear_dynamic_rate(void);

void rtg_set_glide(bool glide);
bool rtg_get_glide(void);

void rtg_set_velocity(uint8_t velocity);
uint8_t rtg_get_velocity(void);

void rtg_set_note_min(uint8_t note_min);
uint8_t rtg_get_note_min(void);

void rtg_set_note_max(uint8_t note_max);
uint8_t rtg_get_note_max(void);

void rtg_set_probability(uint8_t probability);
uint8_t rtg_get_probability(void);

void rtg_set_pattern_length(uint8_t length);
uint8_t rtg_get_pattern_length(void);

void rtg_set_pattern_mask(uint8_t mask);
uint8_t rtg_get_pattern_mask(void);

// Generator type
void rtg_set_generator(rtg_generator_t generator);
rtg_generator_t rtg_get_generator(void);

// Shepard direction
void rtg_set_shepard_direction(shepard_direction_t direction);
shepard_direction_t rtg_get_shepard_direction(void);

// Shepard voice layout (single-channel vs multi-channel)
void rtg_set_shepard_layout(shepard_layout_t layout);
shepard_layout_t rtg_get_shepard_layout(void);

// Shepard fade source (none / CC11 / poly AT)
void rtg_set_shepard_fade(shepard_fade_t fade);
shepard_fade_t rtg_get_shepard_fade(void);

// Shepard smoothness style (stream / wide / crossfade)
void rtg_set_shepard_style(shepard_style_t style);
shepard_style_t rtg_get_shepard_style(void);

// Shepard Wide retrigger spacing in semitones (2..4)
void rtg_set_shepard_wide_semis(uint8_t semis);
uint8_t rtg_get_shepard_wide_semis(void);

// String conversion utilities
const char* rtg_mode_to_string(rtg_mode_t mode);
rtg_mode_t rtg_mode_from_string(const char* str);

const char* rtg_rate_mode_to_string(rtg_rate_mode_t mode);
rtg_rate_mode_t rtg_rate_mode_from_string(const char* str);

const char* rtg_start_mode_to_string(rtg_start_mode_t mode);
rtg_start_mode_t rtg_start_mode_from_string(const char* str);

const char* rtg_generator_to_string(rtg_generator_t gen);
rtg_generator_t rtg_generator_from_string(const char* str);

const char* shepard_direction_to_string(shepard_direction_t dir);
shepard_direction_t shepard_direction_from_string(const char* str);

const char* shepard_layout_to_string(shepard_layout_t layout);
shepard_layout_t shepard_layout_from_string(const char* str);

const char* shepard_fade_to_string(shepard_fade_t fade);
shepard_fade_t shepard_fade_from_string(const char* str);

const char* shepard_style_to_string(shepard_style_t style);
shepard_style_t shepard_style_from_string(const char* str);

// Apply start mode (called when scene loads, after rtg_apply_config)
void rtg_apply_start_mode(void);

// Toggle RTG enabled state (for RTG Toggle action)
void rtg_toggle(void);

#endif // RTG_H
