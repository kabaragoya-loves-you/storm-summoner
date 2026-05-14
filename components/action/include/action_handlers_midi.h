#ifndef ACTION_HANDLERS_MIDI_H
#define ACTION_HANDLERS_MIDI_H

// Public surface of the MIDI action handler.
//
// The dispatch entry point lives in action_internal.h; this header only
// exposes hooks that other components legitimately need to call.

// Release every NoteOn that was sent by ACTION_NOTE press handlers and
// has not yet been matched by a corresponding release. Idempotent.
//
// Called by the local-MIDI silence orchestrator (midi_local_output.h) when
// the device transitions into programming mode or changes scene -- the user
// may have a pad held, so the press already sent NoteOn but the release will
// be suppressed by the silence predicate.
void action_handlers_midi_release_notes(void);

#endif  // ACTION_HANDLERS_MIDI_H
