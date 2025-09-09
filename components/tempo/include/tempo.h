#ifndef _TEMPO_H
#define _TEMPO_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
  CLOCK_SOURCE_INTERNAL,
  CLOCK_SOURCE_MIDI,
  CLOCK_SOURCE_SYNC
} tempo_clock_source_t;

typedef enum {
  DIVIDER_QUARTER = 24,
  DIVIDER_EIGHTH  = 12,
  DIVIDER_SIXTEENTH = 6
} tempo_note_divider_t;

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

// Function to be called from the sync ISR.
void tempo_sync_pulse(void);

// Enable or disable logging/LED blink on every note divider tick.
void tempo_enable_quarter_note_log(bool enable);

// Tap tempo support.
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

#endif /* _TEMPO_H */