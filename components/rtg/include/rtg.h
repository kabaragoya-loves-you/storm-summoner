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

// RTG configuration (stored per-scene)
typedef struct {
  bool enabled;
  rtg_mode_t mode;
  rtg_rate_mode_t rate_mode;    // FREE (Hz) or SYNC (BPM)
  rtg_start_mode_t start_mode;  // When to start RTG on scene load
  uint16_t rate_hz_x100;        // 50-2500 (0.5-25.0 Hz), used when rate_mode == FREE
  uint16_t sync_mult_x1000;     // 125-8000 (0.125x-8.0x), used when rate_mode == SYNC
  bool glide;                   // Use pitch bend smoothing
  uint8_t velocity;             // Note velocity (1-127)
  uint8_t note_min;             // Range floor (default 36/C2)
  uint8_t note_max;             // Range ceiling (default 96/C7)
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

void rtg_set_glide(bool glide);
bool rtg_get_glide(void);

void rtg_set_velocity(uint8_t velocity);
uint8_t rtg_get_velocity(void);

void rtg_set_note_min(uint8_t note_min);
uint8_t rtg_get_note_min(void);

void rtg_set_note_max(uint8_t note_max);
uint8_t rtg_get_note_max(void);

// String conversion utilities
const char* rtg_mode_to_string(rtg_mode_t mode);
rtg_mode_t rtg_mode_from_string(const char* str);

const char* rtg_rate_mode_to_string(rtg_rate_mode_t mode);
rtg_rate_mode_t rtg_rate_mode_from_string(const char* str);

const char* rtg_start_mode_to_string(rtg_start_mode_t mode);
rtg_start_mode_t rtg_start_mode_from_string(const char* str);

// Apply start mode (called when scene loads, after rtg_apply_config)
void rtg_apply_start_mode(void);

// Toggle RTG enabled state (for RTG Toggle action)
void rtg_toggle(void);

#endif // RTG_H
