#ifndef _UARTMIDI_IN_H
#define _UARTMIDI_IN_H

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

typedef struct {
  midi_event_type_t event;
  uint8_t channel;       // Valid for channel messages
  uint8_t data[256];     // For SysEx and other messages; typical channel messages use fewer bytes
  size_t  length;
} midi_message_t;

typedef void (*midi_callback_t)(const midi_message_t *msg, void *user_data);

typedef struct {
  // Channel Voice Messages
  midi_callback_t note_on;
  midi_callback_t note_off;
  midi_callback_t poly_aftertouch;
  midi_callback_t control_change;
  midi_callback_t program_change;
  midi_callback_t channel_aftertouch;
  midi_callback_t pitch_bend;
  // System Common Messages
  midi_callback_t time_code;
  midi_callback_t song_position;
  midi_callback_t song_select;
  midi_callback_t tune_request;
  // System Exclusive
  midi_callback_t sys_ex;
  // Realtime Messages
  midi_callback_t realtime_clock;
  midi_callback_t realtime_tick;
  midi_callback_t realtime_start;
  midi_callback_t realtime_continue;
  midi_callback_t realtime_stop;
  midi_callback_t realtime_reset;
  midi_callback_t active_sensing;
  // Default callback for any unhandled message
  midi_callback_t default_callback;
  void *user_data;
} uartmidi_in_callbacks_t;

void uartmidi_in_init(const uartmidi_in_callbacks_t *callbacks);
void uartmidi_in_process_stream(const uint8_t *data, size_t len);

#endif /* _UARTMIDI_IN_H */
