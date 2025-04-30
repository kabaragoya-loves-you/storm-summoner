#include "midi_in.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "MIDI_IN"
#define UARTMIDI_NUM       UART_NUM_1
#define RX_BUF_SIZE        256
#define UART_READ_TIMEOUT  20  // in milliseconds

// State for channel voice messages (running status)
static uint8_t running_status = 0;
static uint8_t channel_data_buffer[2];
static int channel_data_expected = 0;
static int channel_data_count = 0;

// State for system common messages
static uint8_t sys_common_status = 0;
static uint8_t sys_common_data_buffer[2];
static int sys_common_data_expected = 0;
static int sys_common_data_count = 0;

// State for SysEx messages
static bool in_sysex = false;
static uint8_t sys_ex_buffer[256];
static size_t sys_ex_index = 0;

static midi_in_callbacks_t callbacks_inst = {0};

static void process_byte(uint8_t byte)
{
  // Handle Realtime messages (0xF8-0xFF) immediately.
  if (byte >= 0xF8) {
    midi_message_t msg = {0};
    msg.length = 1;
    msg.data[0] = byte;
    switch (byte) {
      case 0xF8:
        msg.event = MIDI_EVENT_REALTIME_CLOCK;
        if (callbacks_inst.realtime_clock)
          callbacks_inst.realtime_clock(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xF9:
        msg.event = MIDI_EVENT_REALTIME_TICK;
        if (callbacks_inst.realtime_tick)
          callbacks_inst.realtime_tick(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xFA:
        msg.event = MIDI_EVENT_REALTIME_START;
        if (callbacks_inst.realtime_start)
          callbacks_inst.realtime_start(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xFB:
        msg.event = MIDI_EVENT_REALTIME_CONTINUE;
        if (callbacks_inst.realtime_continue)
          callbacks_inst.realtime_continue(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xFC:
        msg.event = MIDI_EVENT_REALTIME_STOP;
        if (callbacks_inst.realtime_stop)
          callbacks_inst.realtime_stop(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xFE:
        msg.event = MIDI_EVENT_ACTIVE_SENSING;
        if (callbacks_inst.active_sensing)
          callbacks_inst.active_sensing(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      case 0xFF:
        msg.event = MIDI_EVENT_REALTIME_RESET;
        if (callbacks_inst.realtime_reset)
          callbacks_inst.realtime_reset(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
      default:
        msg.event = MIDI_EVENT_UNKNOWN;
        if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
        break;
    }
    return;
  }

  // SysEx Handling
  if (byte == 0xF0) {
    in_sysex = true;
    sys_ex_index = 0;
    sys_ex_buffer[sys_ex_index++] = byte;
    return;
  }
  if (in_sysex) {
    if (sys_ex_index < sizeof(sys_ex_buffer))
      sys_ex_buffer[sys_ex_index++] = byte;
    else
      ESP_LOGW(TAG, "SysEx buffer overflow");
    if (byte == 0xF7) {
      midi_message_t msg = {0};
      msg.event = MIDI_EVENT_SYS_EX;
      memcpy(msg.data, sys_ex_buffer, sys_ex_index);
      msg.length = sys_ex_index;
      if (callbacks_inst.sys_ex)
        callbacks_inst.sys_ex(&msg, callbacks_inst.user_data);
      else if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      in_sysex = false;
    }
    return;
  }

  // System Common Messages (0xF1, 0xF2, 0xF3, 0xF6, and undefined 0xF4, 0xF5)
  if (byte >= 0xF0) {
    if (byte == 0xF7) {
      // Unexpected EOX outside of SysEx.
      midi_message_t msg = {0};
      msg.event = MIDI_EVENT_UNKNOWN;
      msg.data[0] = byte;
      msg.length = 1;
      if (callbacks_inst.default_callback)
        callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      return;
    }
    // Start system common message.
    sys_common_status = byte;
    sys_common_data_count = 0;
    switch (sys_common_status) {
      case 0xF1: sys_common_data_expected = 1; break;
      case 0xF2: sys_common_data_expected = 2; break;
      case 0xF3: sys_common_data_expected = 1; break;
      case 0xF6: sys_common_data_expected = 0; break;
      default:   sys_common_data_expected = 0; break;
    }
    if (sys_common_data_expected == 0) {
      midi_message_t msg = {0};
      msg.data[0] = sys_common_status;
      msg.length = 1;
      if (sys_common_status == 0xF6) {
        msg.event = MIDI_EVENT_TUNE_REQUEST;
        if (callbacks_inst.tune_request)
          callbacks_inst.tune_request(&msg, callbacks_inst.user_data);
        else if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      } else {
        msg.event = MIDI_EVENT_UNKNOWN;
        if (callbacks_inst.default_callback)
          callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
      }
      sys_common_status = 0;
    }
    return;
  }
  if (sys_common_status) {
    if (sys_common_data_count < sys_common_data_expected) {
      sys_common_data_buffer[sys_common_data_count++] = byte;
    }
    if (sys_common_data_count == sys_common_data_expected) {
      midi_message_t msg = {0};
      msg.data[0] = sys_common_status;
      memcpy(&msg.data[1], sys_common_data_buffer, sys_common_data_expected);
      msg.length = sys_common_data_expected + 1;
      switch (sys_common_status) {
        case 0xF1:
          msg.event = MIDI_EVENT_TIME_CODE;
          if (callbacks_inst.time_code)
            callbacks_inst.time_code(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xF2:
          msg.event = MIDI_EVENT_SONG_POSITION;
          if (callbacks_inst.song_position)
            callbacks_inst.song_position(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xF3:
          msg.event = MIDI_EVENT_SONG_SELECT;
          if (callbacks_inst.song_select)
            callbacks_inst.song_select(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        default:
          msg.event = MIDI_EVENT_UNKNOWN;
          if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
      }
      sys_common_status = 0;
      sys_common_data_count = 0;
    }
    return;
  }

  // Channel Voice Messages (Running Status)
  if (byte & 0x80) {
    running_status = byte;
    channel_data_expected = 0;
    channel_data_count = 0;
    uint8_t status_nibble = running_status & 0xF0;
    switch (status_nibble) {
      case 0x80:
      case 0x90:
      case 0xA0:
      case 0xB0:
      case 0xE0:
        channel_data_expected = 2;
        break;
      case 0xC0:
      case 0xD0:
        channel_data_expected = 1;
        break;
      default:
        channel_data_expected = 0;
        break;
    }
    return;
  }
  if (running_status) {
    if (channel_data_count < channel_data_expected) {
      channel_data_buffer[channel_data_count++] = byte;
    }
    if (channel_data_count == channel_data_expected) {
      midi_message_t msg = {0};
      msg.data[0] = running_status;
      memcpy(&msg.data[1], channel_data_buffer, channel_data_expected);
      msg.length = channel_data_expected + 1;
      msg.channel = running_status & 0x0F;
      uint8_t status_nibble = running_status & 0xF0;
      switch (status_nibble) {
        case 0x80:
          msg.event = MIDI_EVENT_NOTE_OFF;
          if (callbacks_inst.note_off)
            callbacks_inst.note_off(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0x90:
          msg.event = MIDI_EVENT_NOTE_ON;
          if (callbacks_inst.note_on)
            callbacks_inst.note_on(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xA0:
          msg.event = MIDI_EVENT_POLY_AFTERTOUCH;
          if (callbacks_inst.poly_aftertouch)
            callbacks_inst.poly_aftertouch(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xB0:
          msg.event = MIDI_EVENT_CONTROL_CHANGE;
          if (callbacks_inst.control_change)
            callbacks_inst.control_change(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xC0:
          msg.event = MIDI_EVENT_PROGRAM_CHANGE;
          if (callbacks_inst.program_change)
            callbacks_inst.program_change(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xD0:
          msg.event = MIDI_EVENT_CHANNEL_AFTERTOUCH;
          if (callbacks_inst.channel_aftertouch)
            callbacks_inst.channel_aftertouch(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        case 0xE0:
          msg.event = MIDI_EVENT_PITCH_BEND;
          if (callbacks_inst.pitch_bend)
            callbacks_inst.pitch_bend(&msg, callbacks_inst.user_data);
          else if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
        default:
          msg.event = MIDI_EVENT_UNKNOWN;
          if (callbacks_inst.default_callback)
            callbacks_inst.default_callback(&msg, callbacks_inst.user_data);
          break;
      }
      channel_data_count = 0;
    }
  }
}

void midi_in_process_stream(const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    process_byte(data[i]);
  }
}

static void midi_in_task(void *pvParameters)
{
  uint8_t rx_buf[RX_BUF_SIZE];
  while (1) {
    int len = uart_read_bytes(UARTMIDI_NUM, rx_buf, RX_BUF_SIZE,
                              UART_READ_TIMEOUT / portTICK_PERIOD_MS);
    if (len > 0) {
      // ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);
      midi_in_process_stream(rx_buf, len);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void midi_in_init(const midi_in_callbacks_t *callbacks)
{
  if (callbacks) {
    callbacks_inst = *callbacks;
  }
  xTaskCreate(midi_in_task, "midi_in_task", 4096, NULL, 5, NULL);
  ESP_LOGI(TAG, "MIDI IN initialized");
}
