#include "midi_send_console.h"
#include "midi_messages.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "midi_send";

// Global send command function
static int cmd_send(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGE(TAG, "Usage: send <message_type> [args...]");
    ESP_LOGI(TAG, "Message types: note_on, note_off, cc, pc, song_select, pitch_bend, etc.");
    return 1;
  }
  
  const char* msg_type = argv[1];
  
  // note_on <channel> <note> <velocity>
  if (strcmp(msg_type, "note_on") == 0) {
    if (argc != 5) {
      ESP_LOGE(TAG, "Usage: send note_on <channel> <note> <velocity>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t note = atoi(argv[3]);
    uint8_t vel = atoi(argv[4]);
    send_note_on(ch, note, vel);
    ESP_LOGI(TAG, "Sent note_on: ch=%d note=%d vel=%d", ch, note, vel);
    return 0;
  }
  
  // note_off <channel> <note> <velocity>
  if (strcmp(msg_type, "note_off") == 0) {
    if (argc != 5) {
      ESP_LOGE(TAG, "Usage: send note_off <channel> <note> <velocity>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t note = atoi(argv[3]);
    uint8_t vel = atoi(argv[4]);
    send_note_off(ch, note, vel);
    ESP_LOGI(TAG, "Sent note_off: ch=%d note=%d vel=%d", ch, note, vel);
    return 0;
  }
  
  // cc <channel> <controller> <value>
  if (strcmp(msg_type, "cc") == 0) {
    if (argc != 5) {
      ESP_LOGE(TAG, "Usage: send cc <channel> <controller> <value>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t cc = atoi(argv[3]);
    uint8_t val = atoi(argv[4]);
    send_control_change(ch, cc, val);
    ESP_LOGI(TAG, "Sent CC: ch=%d cc=%d val=%d", ch, cc, val);
    return 0;
  }
  
  // pc <channel> <program>
  if (strcmp(msg_type, "pc") == 0) {
    if (argc != 4) {
      ESP_LOGE(TAG, "Usage: send pc <channel> <program>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t prog = atoi(argv[3]);
    send_program_change(ch, prog);
    ESP_LOGI(TAG, "Sent program change: ch=%d prog=%d", ch, prog);
    return 0;
  }
  
  // song_select <song>
  if (strcmp(msg_type, "song_select") == 0) {
    if (argc != 3) {
      ESP_LOGE(TAG, "Usage: send song_select <song>");
      return 1;
    }
    uint8_t song = atoi(argv[2]);
    send_song_select(song);
    ESP_LOGI(TAG, "Sent song select: %d", song);
    return 0;
  }
  
  // pitch_bend <channel> <value>
  if (strcmp(msg_type, "pitch_bend") == 0) {
    if (argc != 4) {
      ESP_LOGE(TAG, "Usage: send pitch_bend <channel> <value>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    int16_t val = atoi(argv[3]);
    send_pitch_bend(ch, val);
    ESP_LOGI(TAG, "Sent pitch bend: ch=%d val=%d", ch, val);
    return 0;
  }
  
  // aftertouch <channel> <pressure>
  if (strcmp(msg_type, "aftertouch") == 0) {
    if (argc != 4) {
      ESP_LOGE(TAG, "Usage: send aftertouch <channel> <pressure>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t press = atoi(argv[3]);
    send_channel_aftertouch(ch, press);
    ESP_LOGI(TAG, "Sent channel aftertouch: ch=%d pressure=%d", ch, press);
    return 0;
  }
  
  // poly_aftertouch <channel> <note> <pressure>
  if (strcmp(msg_type, "poly_aftertouch") == 0) {
    if (argc != 5) {
      ESP_LOGE(TAG, "Usage: send poly_aftertouch <channel> <note> <pressure>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t note = atoi(argv[3]);
    uint8_t press = atoi(argv[4]);
    send_poly_aftertouch(ch, note, press);
    ESP_LOGI(TAG, "Sent poly aftertouch: ch=%d note=%d pressure=%d", ch, note, press);
    return 0;
  }
  
  // all_notes_off <channel>
  if (strcmp(msg_type, "all_notes_off") == 0) {
    if (argc != 3) {
      ESP_LOGE(TAG, "Usage: send all_notes_off <channel>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    send_all_notes_off(ch);
    ESP_LOGI(TAG, "Sent all notes off: ch=%d", ch);
    return 0;
  }
  
  // reset
  if (strcmp(msg_type, "reset") == 0) {
    send_reset();
    ESP_LOGI(TAG, "Sent MIDI reset");
    return 0;
  }
  
  // clock
  if (strcmp(msg_type, "clock") == 0) {
    send_clock();
    ESP_LOGI(TAG, "Sent MIDI clock");
    return 0;
  }
  
  // start
  if (strcmp(msg_type, "start") == 0) {
    send_start();
    ESP_LOGI(TAG, "Sent MIDI start");
    return 0;
  }
  
  // stop
  if (strcmp(msg_type, "stop") == 0) {
    send_stop();
    ESP_LOGI(TAG, "Sent MIDI stop");
    return 0;
  }
  
  // continue
  if (strcmp(msg_type, "continue") == 0) {
    send_continue();
    ESP_LOGI(TAG, "Sent MIDI continue");
    return 0;
  }
  
  // active_sensing
  if (strcmp(msg_type, "active_sensing") == 0) {
    send_active_sensing();
    ESP_LOGI(TAG, "Sent active sensing");
    return 0;
  }
  
  // song_position <position>
  if (strcmp(msg_type, "song_position") == 0) {
    if (argc != 3) {
      ESP_LOGE(TAG, "Usage: send song_position <position>");
      return 1;
    }
    uint16_t pos = atoi(argv[2]);
    send_song_position(pos);
    ESP_LOGI(TAG, "Sent song position: %d", pos);
    return 0;
  }
  
  // double_cc <channel> <msb_cc> <lsb_cc> <value>
  if (strcmp(msg_type, "double_cc") == 0) {
    if (argc != 6) {
      ESP_LOGE(TAG, "Usage: send double_cc <channel> <msb_cc> <lsb_cc> <value>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint8_t msb = atoi(argv[3]);
    uint8_t lsb = atoi(argv[4]);
    uint16_t val = atoi(argv[5]);
    send_double_control_change(ch, msb, lsb, val);
    ESP_LOGI(TAG, "Sent double CC: ch=%d msb=%d lsb=%d val=%d", ch, msb, lsb, val);
    return 0;
  }
  
  // nrpn <channel> <parameter> <value>
  if (strcmp(msg_type, "nrpn") == 0) {
    if (argc != 5) {
      ESP_LOGE(TAG, "Usage: send nrpn <channel> <parameter> <value>");
      return 1;
    }
    uint8_t ch = atoi(argv[2]);
    uint16_t param = atoi(argv[3]);
    uint16_t val = atoi(argv[4]);
    send_nrpn(ch, param, val);
    ESP_LOGI(TAG, "Sent NRPN: ch=%d param=%d val=%d", ch, param, val);
    return 0;
  }
  
  // tune_request
  if (strcmp(msg_type, "tune_request") == 0) {
    send_tune_request();
    ESP_LOGI(TAG, "Sent tune request");
    return 0;
  }
  
  ESP_LOGE(TAG, "Unknown message type: %s", msg_type);
  return 1;
}

esp_err_t midi_send_console_register(void) {
  const esp_console_cmd_t send_cmd = {
    .command = "send",
    .help = "Send MIDI message (available in all contexts)",
    .hint = "<msg_type> [args...]",
    .func = &cmd_send,
  };
  return esp_console_cmd_register(&send_cmd);
}

