#include "uartmidi_out.h"
#include "midi_out.h"

#include <stdlib.h>
#include <string.h>

#define TAG "MIDI OUT"

void send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  uint8_t message[3] = {
    0x90 | (channel & 0x0F), // Note On, Channel
    note & 0x7F,             // Note Number
    velocity & 0x7F          // Velocity
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  uint8_t message[3] = {
    0x80 | (channel & 0x0F), // Note Off, Channel
    note & 0x7F,             // Note Number
    velocity & 0x7F          // Release Velocity
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
  uint8_t message[3] = {
    0xB0 | (channel & 0x0F), // Control Change, Channel
    controller & 0x7F,       // Controller Number (e.g., Mod Wheel = 1)
    value & 0x7F             // Controller Value
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_program_change(uint8_t channel, uint8_t program) {
  uint8_t message[2] = {
    0xC0 | (channel & 0x0F), // Program Change, Channel
    program & 0x7F           // Program Number
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_song_select(uint8_t song_number) {
  uint8_t message[2] = {0xF3, song_number & 0x7F};
  uartmidi_send_message(message, sizeof(message));
}


void send_pitch_bend(uint8_t channel, int16_t value) {
  value += 8192; // Convert from signed (-8192 to +8191) to unsigned (0-16383)
  uint8_t message[3] = {
    0xE0 | (channel & 0x0F), // Pitch Bend, Channel
    value & 0x7F,            // LSB (Least Significant Byte)
    (value >> 7) & 0x7F      // MSB (Most Significant Byte)
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_channel_aftertouch(uint8_t channel, uint8_t pressure) {
  uint8_t message[2] = {
    0xD0 | (channel & 0x0F), // Channel Aftertouch, Channel
    pressure & 0x7F          // Pressure value (0-127)
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_poly_aftertouch(uint8_t channel, uint8_t note, uint8_t pressure) {
  uint8_t message[3] = {
    0xA0 | (channel & 0x0F), // Polyphonic Aftertouch, Channel
    note & 0x7F,             // Note Number
    pressure & 0x7F          // Pressure value (0-127)
  };
  uartmidi_send_message(message, sizeof(message));
}

void send_all_notes_off(uint8_t channel) {
  send_control_change(channel, 123, 0);  // CC#123: All Notes Off
}

void send_reset() {
  uint8_t message = 0xFF; // System Reset (panic button!)
  uartmidi_send_message(&message, 1);
}

// Send a System Exclusive (SysEx) message (customizable)
/*
uint8_t sysex_data[] = {0x7E, 0x00, 0x06, 0x01};
send_sysex(sysex_data, sizeof(sysex_data));
uint8_t gm_reset_data[] = {0x7E, 0x7F, 0x09, 0x01};
send_sysex(gm_reset_data, sizeof(gm_reset_data));
*/
void send_sysex(const uint8_t *data, size_t length) {
  if (length == 0) return; // No data to send

  uint8_t *message = malloc(length + 2); // +2 for 0xF0 and 0xF7
  if (!message) return; // Allocation failed

  message[0] = 0xF0; // SysEx start
  memcpy(&message[1], data, length);
  message[length + 1] = 0xF7; // SysEx end

  uartmidi_send_message(message, length + 2);
  free(message);
}


// Send MIDI Clock (24 messages per beat for sync)
void send_clock() {
  const uint8_t message = 0xF8; // Timing Clock
  uartmidi_send_message(&message, 1);
}

void send_time_code(uint8_t message_type, uint8_t values) {
  uint8_t data_byte = ((message_type & 0x07) << 4) | (values & 0x0F);
  const uint8_t message[2] = {0xF1, data_byte};
  uartmidi_send_message(message, 2);
}

// Send Start (Begin playing a sequence)
void send_start() {
  uint8_t message = 0xFA; // Start
  uartmidi_send_message(&message, 1);
}

// Send Stop (Stop playing a sequence)
void send_stop() {
  uint8_t message = 0xFC; // Stop
  uartmidi_send_message(&message, 1);
}

// Send Continue (Resume playing a sequence)
void send_continue() {
  uint8_t message = 0xFB; // Continue
  uartmidi_send_message(&message, 1);
}

// send_double_control_change(channel, 1, 33, value);
void send_double_control_change(uint8_t channel, uint8_t msb_cc, uint8_t lsb_cc, uint16_t value) {
  uint8_t msb_value = (value >> 7) & 0x7F; // Upper 7 bits
  uint8_t lsb_value = value & 0x7F;        // Lower 7 bits

  const uint8_t msb_message[3] = {0xB0 | (channel & 0x0F), msb_cc & 0x7F, msb_value};
  const uint8_t lsb_message[3] = {0xB0 | (channel & 0x0F), lsb_cc & 0x7F, lsb_value};

  uartmidi_send_message(msb_message, 3);
  uartmidi_send_message(lsb_message, 3);
}

// send this every 250ms in a task
void send_active_sensing() {
  uint8_t message = 0xFE;
  uartmidi_send_message(&message, 1);
}

void send_song_position(uint16_t position) {
  uint8_t message[3] = {
    0xF2,                // Song Position Pointer
    position & 0x7F,     // LSB
    (position >> 7) & 0x7F  // MSB
  };
  uartmidi_send_message(message, sizeof(message));
}

/*
MMC Command	Function
0x06 0x01	Start
0x06 0x02	Stop
0x06 0x03	Pause
0x06 0x04	Record
*/
void send_mmc(uint8_t command) {
  uint8_t message[] = {0xF0, 0x7F, 0x7F, 0x06, command, 0xF7};
  uartmidi_send_message(message, sizeof(message));
}

uint8_t last_status_byte = 0; // Tracks last status to avoid redundancy

//  send_note_on_optimized(0, 60, 100); // Channel 1 is represented by 0
void send_note_on_optimized(uint8_t channel, uint8_t note, uint8_t velocity) {
  uint8_t status_byte = 0x90 | (channel & 0x0F); // Note On message for the specified channel

  // If the current status byte is different from the last one, send the status byte
  if (status_byte != last_status_byte) {
      uartmidi_send_message(&status_byte, 1);
      last_status_byte = status_byte;
  }

  // Send the note and velocity data bytes
  uartmidi_send_message(&note, 1);
  uartmidi_send_message(&velocity, 1);
}

// Set Filter Cutoff (NRPN #513, value 8192)
//  send_nrpn(0, 2, 1, 8192);
void send_nrpn(uint8_t channel, uint16_t parameter, uint16_t value) {
  uint8_t param_msb = (parameter >> 7) & 0x7F;
  uint8_t param_lsb = parameter & 0x7F;
  uint8_t value_msb = (value >> 7) & 0x7F;
  uint8_t value_lsb = value & 0x7F;

  const uint8_t nrpn_messages[4][3] = {
      {0xB0 | (channel & 0x0F), 99, param_msb}, // NRPN MSB
      {0xB0 | (channel & 0x0F), 98, param_lsb}, // NRPN LSB
      {0xB0 | (channel & 0x0F), 6, value_msb},  // Data Entry MSB
      {0xB0 | (channel & 0x0F), 38, value_lsb}  // Data Entry LSB
  };

  for (int i = 0; i < 4; ++i) {
      uartmidi_send_message(nrpn_messages[i], 3);
  }
}

/*
  uint16_t my_tuning[128];
  
  // Create a Just Intonation scale
  for (int i = 0; i < 128; i++) {
    my_tuning[i] = 8192; // Keep tuning at default for now
  }
  my_tuning[60] = 8300; // Tune Middle C slightly up

  midi_send_mts_full(0, my_tuning);
*/
void send_mts_full(uint8_t channel, uint16_t *tuning_data) {
  uint8_t message[266]; // SysEx message size for 128 notes
  message[0] = 0xF0;
  message[1] = 0x7F;
  message[2] = 0x7F; // Device ID (Global)
  message[3] = 0x08; // MTS
  message[4] = 0x01; // Bulk tuning

  for (int i = 0; i < 128; i++) {
    message[5 + (i * 2)] = (tuning_data[i] >> 7) & 0x7F; // MSB
    message[6 + (i * 2)] = tuning_data[i] & 0x7F;        // LSB
  }

  message[265] = 0xF7; // End SysEx
  uartmidi_send_message(message, sizeof(message));
}

void send_tune_request() {
  uint8_t message = 0xF6; // Tune Request
  uartmidi_send_message(&message, 1);
}
