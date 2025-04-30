#ifndef _MIDI_MESSAGES_H
#define _MIDI_MESSAGES_H

#include <stddef.h>
#include <stdint.h>

void send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void send_note_off(uint8_t channel, uint8_t note, uint8_t velocity);
void send_control_change(uint8_t channel, uint8_t controller, uint8_t value);
void send_program_change(uint8_t channel, uint8_t program);
void send_song_select(uint8_t song_number);
void send_pitch_bend(uint8_t channel, int16_t value);
void send_channel_aftertouch(uint8_t channel, uint8_t pressure);
void send_poly_aftertouch(uint8_t channel, uint8_t note, uint8_t pressure);
void send_all_notes_off(uint8_t channel);
void send_reset(void);
void send_sysex(const uint8_t *data, size_t length);
void send_clock();
void send_time_code(uint8_t message_type, uint8_t values);
void send_start();
void send_stop();
void send_continue();
void send_double_control_change(uint8_t channel, uint8_t msb_cc, uint8_t lsb_cc, uint16_t value);
void send_active_sensing();
void send_song_position(uint16_t position);
void send_mmc(uint8_t command);
void send_note_on_optimized(uint8_t channel, uint8_t note, uint8_t velocity);
void send_nrpn(uint8_t channel, uint16_t parameter, uint16_t value);
void send_mts_single(uint8_t channel, uint8_t note, int16_t pitch_offset);
void send_mts_full(uint8_t channel, uint16_t *tuning_data);
void send_tune_request();

#endif /* _MIDI_MESSAGES_H */
