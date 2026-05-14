#ifndef MIDI_LOCAL_OUTPUT_H
#define MIDI_LOCAL_OUTPUT_H

#include <stdbool.h>

// Single source of truth for "are on-device producers allowed to emit MIDI?".
//
// On-device note/CC producers (RTG, LFO, sensor scene handlers, action handlers,
// touchwheel notes, CV/Gate, etc.) consult this predicate before emitting.
// Inbound MIDI from UART/USB and the passthrough path are NOT gated by this --
// passthrough has its own cut flag (see midi_out_set_cut_passthrough).
//
// State transitions only at the PERFORMANCE <-> PROGRAMMING boundary; the
// screensaver mode is transparent and never changes silence state.
bool midi_local_output_is_enabled(void);

// Release any held notes from every on-device producer. Used both by the
// silence transition (programming mode entry) and by scene change. Producers
// each own their own "release my held notes" function; this is the fixed-list
// orchestration over all of them.
void midi_local_output_release_all(void);

// Mode transition: release everything, then mute clock, stop LFO loops, and
// flip the predicate so producers stop emitting. Call when transitioning from
// PERFORMANCE -> PROGRAMMING.
void midi_local_output_silence(void);

// Inverse of silence(). Unmutes clock, flips the predicate. LFO restoration
// stays in scene_resume_input (it depends on saved running-state there).
void midi_local_output_enable(void);

#endif  // MIDI_LOCAL_OUTPUT_H
