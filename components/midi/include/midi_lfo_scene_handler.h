#ifndef MIDI_LFO_SCENE_HANDLER_H
#define MIDI_LFO_SCENE_HANDLER_H

#include <stdint.h>
#include "esp_err.h"

// Initialize the LFO scene handler
// Subscribes to EVENT_LFO1_VALUE and EVENT_LFO2_VALUE
esp_err_t midi_lfo_scene_handler_init(void);

// Release any active notes (for programming mode, scene change, etc.)
void midi_lfo_scene_handler_release_notes(void);

// Release the active note for a single slot (0=LFO1, 1=LFO2). Used by
// individual-slot disable paths (menu page, ACTION_LFO + VARIANT_STOP /
// VARIANT_TOGGLE) so a NOTE-output mapping doesn't sustain when its LFO
// loop is turned off.
void midi_lfo_scene_handler_release_notes_for_slot(uint8_t slot);

// Send restore value for specified LFO slot(s) when LFO stops
// slot: 0=LFO1, 1=LFO2, or call twice for both
// Sends phase-0 waveform value through curve/polarity to CC(s)
void midi_lfo_scene_handler_restore_value(uint8_t slot);

#endif // MIDI_LFO_SCENE_HANDLER_H
