#ifndef _TEMPO_H
#define _TEMPO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Clock source types
typedef enum {
  CLOCK_SOURCE_INTERNAL,
  CLOCK_SOURCE_MIDI,
  CLOCK_SOURCE_SYNC
} tempo_clock_source_t;

// LED operational modes (day/night)
typedef enum {
  LED_MODE_DAYLIGHT,    // Off by default, turn on explicitly
  LED_MODE_NIGHTTIME    // On by default, turn off explicitly (inverted)
} led_mode_t;

typedef enum {
  CLOCK_OUTPUT_NONE = 0,
  CLOCK_OUTPUT_USB = 1,
  CLOCK_OUTPUT_UART = 2,
  CLOCK_OUTPUT_BOTH = 3
} clock_output_t;

typedef enum {
  CLOCK_STANDARD_24PPQN = 0,    // DIN Sync (24 pulses per quarter note)
  CLOCK_STANDARD_16TH_NOTE,     // Korg Volca (pulse per 16th note)
  CLOCK_STANDARD_BEAT           // Modular (1 pulse per beat)
} tempo_clock_standard_t;

typedef enum {
  DIVIDER_QUARTER = 24,
  DIVIDER_EIGHTH  = 12,
  DIVIDER_SIXTEENTH = 6
} tempo_note_divider_t;

// Tap tempo session modes
typedef enum {
  TAP_MODE_TOGGLE,  // Start on first tap_tempo, stop on second (default)
  TAP_MODE_TIME,    // Sample for N seconds after tap_tempo triggered
  TAP_MODE_HOLD     // Sample while tap_tempo trigger is held
} tap_tempo_mode_t;

// Time signature structure
typedef struct {
  uint8_t numerator;    // Beats per bar (e.g., 4 for 4/4)
  uint8_t denominator;  // Beat unit (e.g., 4 for quarter note)
} time_signature_t;

void tempo_init(void);

// Start and stop the tempo module tasks.
void tempo_start(void);
void tempo_stop(void);

// Set and get the global BPM.
void tempo_set_bpm(uint16_t bpm);
uint16_t tempo_get_bpm(void);

// Set the clock source.
void tempo_set_source(tempo_clock_source_t source);
tempo_clock_source_t tempo_get_source(void);

// Function to be called from the sync ISR.
void tempo_sync_pulse(void);

// Enable or disable logging/LED blink on every note divider tick.
void tempo_enable_quarter_note_log(bool enable);

// Tap tempo support
void tempo_tap(void);                      // Register a single tap input
void tempo_tap_session_start(void);        // Start tap tempo sampling session
void tempo_tap_session_stop(void);         // Stop tap tempo sampling session  
void tempo_tap_session_toggle(void);       // Toggle sampling session (for toggle mode)
bool tempo_is_tap_sampling(void);          // Check if currently sampling taps

// Tap tempo session configuration
void tempo_set_tap_mode(tap_tempo_mode_t mode);
tap_tempo_mode_t tempo_get_tap_mode(void);
void tempo_set_tap_timeout(uint8_t seconds);  // For TAP_MODE_TIME (default 10s)
uint8_t tempo_get_tap_timeout(void);

// Legacy compatibility (deprecated, use tempo_tap instead)
void tempo_tap_event(void);

// For MIDI clock mode.
void tempo_midi_clock_tick(void);

// Set and get the note divider.
void tempo_set_note_divider(tempo_note_divider_t divider);
tempo_note_divider_t tempo_get_note_divider(void);

// Time signature management
void tempo_set_time_signature(uint8_t numerator, uint8_t denominator);
time_signature_t tempo_get_time_signature(void);

// LED sync control
void tempo_set_led_sync(bool enabled);
bool tempo_get_led_sync(void);
void tempo_set_led_flash_ratio(uint8_t ratio);
uint8_t tempo_get_led_flash_ratio(void);

// Clock standard control
void tempo_set_clock_standard(tempo_clock_standard_t standard);
tempo_clock_standard_t tempo_get_clock_standard(void);

// BPM change deadzone (0 = no deadzone, 1-5 = ignore ±N BPM changes after locking)
void tempo_set_bpm_deadzone(uint8_t deadzone);
uint8_t tempo_get_bpm_deadzone(void);

// Clock output control
void tempo_set_clock_output(clock_output_t output);
clock_output_t tempo_get_clock_output(void);
void tempo_set_clock_always_send(bool always_send);
bool tempo_get_clock_always_send(void);
void tempo_set_disable_clock_on_passthrough(bool disable);
bool tempo_get_disable_clock_on_passthrough(void);

// ============================================================================
// LED Control (merged from led component)
// ============================================================================

// Initialize LED hardware and settings (call after tempo_init)
void led_init(void);

// Direct LED control
void flash_led(uint32_t duration);  // Flash for duration (non-blocking)
void led_set_on(void);              // Turn LED on (solid)
void led_set_off(void);             // Turn LED off
void led_restore_baseline(void);    // Restore LED to normal day/night mode state

// Day/Night mode
esp_err_t led_set_mode(led_mode_t mode);
led_mode_t led_get_mode(void);

// Sundial mode - auto-switch day/night based on ambient light sensor
// Note: Requires sensor_init() and als_enable() to be called
esp_err_t led_set_sundial_mode(bool enabled);
bool led_get_sundial_mode(void);

// Global enable/disable
void led_set_enabled(bool enabled);
bool led_get_enabled(void);

#endif /* _TEMPO_H */