#ifndef _MIDI_IN_H
#define _MIDI_IN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
  MIDI_EVENT_NOTE_OFF,
  MIDI_EVENT_NOTE_ON,
  MIDI_EVENT_POLY_AFTERTOUCH,
  MIDI_EVENT_CONTROL_CHANGE,
  MIDI_EVENT_PROGRAM_CHANGE,
  MIDI_EVENT_CHANNEL_AFTERTOUCH,
  MIDI_EVENT_PITCH_BEND,
  MIDI_EVENT_TIME_CODE,
  MIDI_EVENT_SONG_POSITION,
  MIDI_EVENT_SONG_SELECT,
  MIDI_EVENT_TUNE_REQUEST,
  MIDI_EVENT_SYS_EX,
  MIDI_EVENT_REALTIME_CLOCK,
  MIDI_EVENT_REALTIME_TICK,
  MIDI_EVENT_REALTIME_START,
  MIDI_EVENT_REALTIME_CONTINUE,
  MIDI_EVENT_REALTIME_STOP,
  MIDI_EVENT_REALTIME_RESET,
  MIDI_EVENT_ACTIVE_SENSING,
  MIDI_EVENT_UNKNOWN
} midi_event_type_t;

/**
 * @brief Initialize MIDI IN system
 * Initializes parser, UART, and USB transports
 */
void midi_in_init(void);

/**
 * @brief Process MIDI byte stream
 * Called by transport layers (UART/USB)
 * @param data MIDI data bytes
 * @param len Length of data
 * @param source Source interface (MIDI_SOURCE_UART, MIDI_SOURCE_USB, etc)
 */
void midi_in_process_stream(const uint8_t *data, size_t len, uint8_t source);

#endif /* _MIDI_IN_H */
