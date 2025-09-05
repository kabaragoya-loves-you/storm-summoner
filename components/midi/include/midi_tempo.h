#ifndef _MIDI_TEMPO_H
#define _MIDI_TEMPO_H

#include <stdint.h>
#include <stdbool.h>
#include "app_settings.h"

typedef enum {
  CLOCK_SOURCE_INTERNAL,
  CLOCK_SOURCE_MIDI,
  CLOCK_SOURCE_SYNC
} midi_clock_source_t;

typedef enum {
  DIVIDER_QUARTER = 24,
  DIVIDER_EIGHTH  = 12,
  DIVIDER_SIXTEENTH = 6
} midi_note_divider_t;

// Initialize the MIDI Tempo module.
void midi_tempo_init(void);

// Start and stop the tempo module tasks.
void midi_tempo_start(void);
void midi_tempo_stop(void);

// Set and get the global BPM.
void midi_tempo_set_bpm(uint16_t bpm);
uint16_t midi_tempo_get_bpm(void);

// Set the clock source.
void midi_tempo_set_source(midi_clock_source_t source);

// Function to be called from the sync ISR.
void midi_tempo_sync_pulse(void);

// Enable or disable logging/LED blink on every note divider tick.
void midi_tempo_enable_quarter_note_log(bool enable);

// Tap tempo support.
void midi_tempo_tap_event(void);

// For MIDI clock mode.
void midi_tempo_midi_clock_tick(void);

// Set and get the note divider.
void midi_tempo_set_note_divider(midi_note_divider_t divider);
midi_note_divider_t midi_tempo_get_note_divider(void);

// Event handler initialization
void midi_tempo_event_handler_init(void);

#endif /* _MIDI_TEMPO_H */
