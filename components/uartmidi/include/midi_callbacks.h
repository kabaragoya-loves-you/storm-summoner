#ifndef _MIDI_CALLBACKS_H
#define _MIDI_CALLBACKS_H

#include "uartmidi_in.h"
#include "midi_tempo.h"
#include "esp_log.h"

void midi_callbacks_init(void);

// MIDI Channel Voice Messages
void note_on(const midi_message_t *msg, void *user_data);
void note_off(const midi_message_t *msg, void *user_data);
void poly_aftertouch(const midi_message_t *msg, void *user_data);
void control_change(const midi_message_t *msg, void *user_data);
void program_change(const midi_message_t *msg, void *user_data);
void channel_aftertouch(const midi_message_t *msg, void *user_data);
void pitch_bend(const midi_message_t *msg, void *user_data);

// System Common Messages
void time_code(const midi_message_t *msg, void *user_data);
void song_position(const midi_message_t *msg, void *user_data);
void song_select(const midi_message_t *msg, void *user_data);
void tune_request(const midi_message_t *msg, void *user_data);
void sys_ex(const midi_message_t *msg, void *user_data);

// System Real-Time Messages
void realtime_clock(const midi_message_t *msg, void *user_data);
void realtime_tick(const midi_message_t *msg, void *user_data);
void realtime_start(const midi_message_t *msg, void *user_data);
void realtime_continue(const midi_message_t *msg, void *user_data);
void realtime_stop(const midi_message_t *msg, void *user_data);
void realtime_reset(const midi_message_t *msg, void *user_data);
void active_sensing(const midi_message_t *msg, void *user_data);

// Default handler for unhandled messages
void default_callback(const midi_message_t *msg, void *user_data);

#endif /* _MIDI_CALLBACKS_H */ 