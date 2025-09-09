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

#endif /* _TRANSPORT_H */
