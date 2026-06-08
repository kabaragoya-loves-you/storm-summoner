#ifndef _TRANSPORT_H
#define _TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Transport states (two-state model: pause is STOPPED with position held)
typedef enum {
  TRANSPORT_STOPPED = 0,
  TRANSPORT_PLAYING
} transport_state_t;

// Transport event sources
typedef enum {
  TRANSPORT_SOURCE_MIDI = 0,
  TRANSPORT_SOURCE_UI,
  TRANSPORT_SOURCE_FOOTSWITCH,
  TRANSPORT_SOURCE_INTERNAL
} transport_source_t;

esp_err_t transport_init(void);

transport_state_t transport_get_state(void);
bool transport_is_playing(void);

// Fresh start from stopped (F2 00 00 + FA). If playing, stops first.
esp_err_t transport_play(void);
// Stop clock (FC); second stop while already stopped relocates to top (F2 00 00).
esp_err_t transport_stop(void);
// Alias for transport_stop() (MMC Pause is not a separate state).
esp_err_t transport_pause(void);
// Resume from stopped position (FB).
esp_err_t transport_resume(void);
// Play locally + MMC Record strobe (no recording state on device).
esp_err_t transport_record(void);

uint32_t transport_get_current_bar(void);
uint8_t transport_get_current_beat(void);
void transport_reset_position(void);

// Song Position Pointer (MIDI beats = 1/16 notes from top).
void transport_set_song_position(uint16_t spp_sixteenths);

#endif /* _TRANSPORT_H */
