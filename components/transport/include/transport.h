#ifndef _TRANSPORT_H
#define _TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Transport states
typedef enum {
  TRANSPORT_STOPPED = 0,
  TRANSPORT_PLAYING,
  TRANSPORT_PAUSED,
  TRANSPORT_RECORDING
} transport_state_t;

// Transport event sources
typedef enum {
  TRANSPORT_SOURCE_MIDI = 0,
  TRANSPORT_SOURCE_UI,
  TRANSPORT_SOURCE_FOOTSWITCH,
  TRANSPORT_SOURCE_INTERNAL
} transport_source_t;

/**
 * Initialize the transport component
 * @return ESP_OK on success
 */
esp_err_t transport_init(void);

/**
 * Get current transport state
 * @return Current transport state
 */
transport_state_t transport_get_state(void);

/**
 * Check if transport is playing
 * @return true if playing or recording
 */
bool transport_is_playing(void);

/**
 * Check if transport is recording
 * @return true if recording
 */
bool transport_is_recording(void);

/**
 * Transport control functions
 * Note: play() and record() are toggles - they pause if already in that state
 */
esp_err_t transport_play(void);    // Toggle: playing → pause, else → play
esp_err_t transport_stop(void);
esp_err_t transport_pause(void);   // Pause only (does not unpause)
esp_err_t transport_record(void);  // Toggle: recording → pause, else → record

/**
 * Transport position tracking
 */
uint32_t transport_get_current_bar(void);    // Get current bar number (1-based)
uint8_t transport_get_current_beat(void);    // Get current beat in bar (1-based)
void transport_reset_position(void);         // Reset to bar 1, beat 1

/**
 * Check if transport just stopped (for distinguishing Play vs Resume)
 * @return true if Stop was received within the detection window
 */
bool transport_just_stopped(void);

#endif /* _TRANSPORT_H */
