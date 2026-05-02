#ifndef NOTE_TRACK_CONFIG_H
#define NOTE_TRACK_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Default note range: C2..C6 mapped to 0..127
#define NOTE_TRACK_DEFAULT_LOW_NOTE  36   // C2
#define NOTE_TRACK_DEFAULT_HIGH_NOTE 84   // C6

// Filter mode for incoming Note On/Off messages when note track is enabled
typedef enum {
  NOTE_TRACK_FILTER_INTERCEPT = 0,  // Convert AND pass through to MIDI OUT (default)
  NOTE_TRACK_FILTER_KILL = 1        // Convert and drop from passthrough
} note_track_filter_mode_t;

// Initialize the module: load globals from NVS or apply defaults.
// Note Track is enabled per-scene (scene->note_track.enabled); these globals
// only describe the input range, channel filter, and pass-through behaviour.
esp_err_t note_track_config_init(void);

// Note range: low <= high, both 0..127
uint8_t note_track_get_low_note(void);
esp_err_t note_track_set_low_note(uint8_t note);
uint8_t note_track_get_high_note(void);
esp_err_t note_track_set_high_note(uint8_t note);

// Channel filter: 0 = omni, 1..16 = specific channel
uint8_t note_track_get_channel(void);
esp_err_t note_track_set_channel(uint8_t channel);

// Filter mode: intercept (default) or kill
note_track_filter_mode_t note_track_get_filter_mode(void);
esp_err_t note_track_set_filter_mode(note_track_filter_mode_t mode);

// Returns true when an incoming note on the given (0-15) channel and pitch
// falls inside the configured range and matches the channel filter.
// Does NOT consider whether any scene's per-scene mapping is enabled - that
// gating happens at the call sites that have access to the current scene.
bool note_track_message_matches(uint8_t channel0, uint8_t note);

#ifdef __cplusplus
}
#endif

#endif // NOTE_TRACK_CONFIG_H
