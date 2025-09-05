#include "midi_in.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.h"
#include <string.h>
#include <stdlib.h>
#include "task_priorities.h"

#define TAG "MIDI_IN_EVENT"
#define MIDI_NUM       UART_NUM_1
#define RX_BUF_SIZE        256
#define UART_READ_TIMEOUT  20  // in milliseconds
#define MAX_SYSEX_SIZE     512 // Larger buffer for SysEx

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
static uint8_t* sys_ex_buffer = NULL;
static size_t sys_ex_index = 0;
static size_t sys_ex_capacity = 0;

// Forward declaration for event handler init
void midi_in_event_handler_init(void);

// Helper function to validate MIDI data byte
static inline bool is_valid_data_byte(uint8_t byte) {
  return (byte & 0x80) == 0;
}

// Helper to post MIDI event
static void post_midi_event(midi_event_type_t type, uint8_t channel, uint8_t data1, uint8_t data2, uint8_t status, uint8_t* sysex_data, uint16_t length) {
  event_t midi_event = {
    .type = EVENT_MIDI_IN,
    .priority = (type == MIDI_EVENT_REALTIME_CLOCK || 
      type == MIDI_EVENT_REALTIME_START ||
      type == MIDI_EVENT_REALTIME_STOP) ? EVENT_PRIORITY_HIGH : EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.midi_in = {
      .type = type,
      .channel = channel,
      .data1 = data1,
      .data2 = data2,
      .source = MIDI_SOURCE_UART,
      .raw_status = status,
      .length = length,
      .sysex_data = sysex_data
    }
  };
  
  esp_err_t ret = event_bus_post(&midi_event);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to post MIDI event: %s", esp_err_to_name(ret));
    // If it was a SysEx event, we need to free the data
    if (sysex_data) free(sysex_data);
  }
}

static void process_byte(uint8_t byte) {
  // Handle Realtime messages (0xF8-0xFF) immediately.
  if (byte >= 0xF8) {
    midi_event_type_t event_type;
    switch (byte) {
      case 0xF8: event_type = MIDI_EVENT_REALTIME_CLOCK; break;
      case 0xF9: event_type = MIDI_EVENT_REALTIME_TICK; break;
      case 0xFA: event_type = MIDI_EVENT_REALTIME_START; break;
      case 0xFB: event_type = MIDI_EVENT_REALTIME_CONTINUE; break;
      case 0xFC: event_type = MIDI_EVENT_REALTIME_STOP; break;
      case 0xFE: event_type = MIDI_EVENT_ACTIVE_SENSING; break;
      case 0xFF: event_type = MIDI_EVENT_REALTIME_RESET; break;
      default:   event_type = MIDI_EVENT_UNKNOWN; break;
    }
    post_midi_event(event_type, 0, 0, 0, byte, NULL, 1);
    return;
  }

  // SysEx Handling
  if (byte == 0xF0) {
    // Clear running status on system message
    running_status = 0;
    in_sysex = true;
    sys_ex_index = 0;
    
    // Allocate initial buffer if needed
    if (!sys_ex_buffer) {
      sys_ex_capacity = 256;
      sys_ex_buffer = malloc(sys_ex_capacity);
      if (!sys_ex_buffer) {
        ESP_LOGE(TAG, "Failed to allocate SysEx buffer");
        in_sysex = false;
        return;
      }
    }
    sys_ex_buffer[sys_ex_index++] = byte;
    return;
  }
  
  if (in_sysex) {
    // Grow buffer if needed
    if (sys_ex_index >= sys_ex_capacity) {
      size_t new_capacity = sys_ex_capacity * 2;
      if (new_capacity > MAX_SYSEX_SIZE) {
        ESP_LOGW(TAG, "SysEx message exceeds maximum size, dropping");
        in_sysex = false;
        sys_ex_index = 0;
        return;
      }
      uint8_t* new_buffer = realloc(sys_ex_buffer, new_capacity);
      if (!new_buffer) {
        ESP_LOGE(TAG, "Failed to grow SysEx buffer");
        in_sysex = false;
        sys_ex_index = 0;
        return;
      }
      sys_ex_buffer = new_buffer;
      sys_ex_capacity = new_capacity;
    }
    
    sys_ex_buffer[sys_ex_index++] = byte;
    
    if (byte == 0xF7) {
      // Complete SysEx message - create a copy for the event
      uint8_t* sysex_copy = malloc(sys_ex_index);
      if (sysex_copy) {
        memcpy(sysex_copy, sys_ex_buffer, sys_ex_index);
        post_midi_event(MIDI_EVENT_SYS_EX, 0, 0, 0, 0xF0, sysex_copy, sys_ex_index);
      } else {
        ESP_LOGE(TAG, "Failed to allocate SysEx copy");
      }
      in_sysex = false;
      sys_ex_index = 0;
    }
    return;
  }

  // System Common Messages (0xF1, 0xF2, 0xF3, 0xF6, and undefined 0xF4, 0xF5)
  if (byte >= 0xF0) {
    // Clear running status on system message
    running_status = 0;
    
    if (byte == 0xF7) {
      // Unexpected EOX outside of SysEx.
      post_midi_event(MIDI_EVENT_UNKNOWN, 0, byte, 0, byte, NULL, 1);
      return;
    }
    
    // Start system common message.
    sys_common_status = byte;
    sys_common_data_count = 0;
    switch (sys_common_status) {
      case 0xF1: sys_common_data_expected = 1; break; // Time Code
      case 0xF2: sys_common_data_expected = 2; break; // Song Position
      case 0xF3: sys_common_data_expected = 1; break; // Song Select
      case 0xF6: sys_common_data_expected = 0; break; // Tune Request
      default:   sys_common_data_expected = 0; break;
    }
    
    if (sys_common_data_expected == 0) {
      midi_event_type_t event_type = (sys_common_status == 0xF6) ? MIDI_EVENT_TUNE_REQUEST : MIDI_EVENT_UNKNOWN;
      post_midi_event(event_type, 0, 0, 0, sys_common_status, NULL, 1);
      sys_common_status = 0;
    }
    return;
  }
  
  if (sys_common_status) {
    // Validate data byte
    if (!is_valid_data_byte(byte)) {
      ESP_LOGW(TAG, "Invalid data byte 0x%02X in system common message", byte);
      sys_common_status = 0;
      return;
    }
    
    if (sys_common_data_count < sys_common_data_expected) sys_common_data_buffer[sys_common_data_count++] = byte;
    
    if (sys_common_data_count == sys_common_data_expected) {
      midi_event_type_t event_type;
      switch (sys_common_status) {
        case 0xF1: event_type = MIDI_EVENT_TIME_CODE; break;
        case 0xF2: event_type = MIDI_EVENT_SONG_POSITION; break;
        case 0xF3: event_type = MIDI_EVENT_SONG_SELECT; break;
        default:   event_type = MIDI_EVENT_UNKNOWN; break;
      }
      
      uint8_t data1 = (sys_common_data_expected >= 1) ? sys_common_data_buffer[0] : 0;
      uint8_t data2 = (sys_common_data_expected >= 2) ? sys_common_data_buffer[1] : 0;
      
      post_midi_event(event_type, 0, data1, data2, sys_common_status, NULL, sys_common_data_expected + 1);
      
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
      case 0x80: // Note Off
      case 0x90: // Note On
      case 0xA0: // Poly Aftertouch
      case 0xB0: // Control Change
      case 0xE0: // Pitch Bend
        channel_data_expected = 2;
        break;
      case 0xC0: // Program Change
      case 0xD0: // Channel Aftertouch
        channel_data_expected = 1;
        break;
      default:
        channel_data_expected = 0;
        break;
    }
    return;
  }
  
  if (running_status) {
    // Validate data byte
    if (!is_valid_data_byte(byte)) {
      ESP_LOGW(TAG, "Invalid data byte 0x%02X in channel message", byte);
      running_status = 0;
      return;
    }
    
    if (channel_data_count < channel_data_expected) channel_data_buffer[channel_data_count++] = byte;
    
    if (channel_data_count == channel_data_expected) {
      uint8_t channel = running_status & 0x0F;
      uint8_t status_nibble = running_status & 0xF0;
      midi_event_type_t event_type;
      
      switch (status_nibble) {
        case 0x80: event_type = MIDI_EVENT_NOTE_OFF; break;
        case 0x90: event_type = MIDI_EVENT_NOTE_ON; break;
        case 0xA0: event_type = MIDI_EVENT_POLY_AFTERTOUCH; break;
        case 0xB0: event_type = MIDI_EVENT_CONTROL_CHANGE; break;
        case 0xC0: event_type = MIDI_EVENT_PROGRAM_CHANGE; break;
        case 0xD0: event_type = MIDI_EVENT_CHANNEL_AFTERTOUCH; break;
        case 0xE0: event_type = MIDI_EVENT_PITCH_BEND; break;
        default:   event_type = MIDI_EVENT_UNKNOWN; break;
      }
      
      uint8_t data1 = (channel_data_expected >= 1) ? channel_data_buffer[0] : 0;
      uint8_t data2 = (channel_data_expected >= 2) ? channel_data_buffer[1] : 0;
      
      post_midi_event(event_type, channel, data1, data2, running_status, NULL, channel_data_expected + 1);
      
      channel_data_count = 0;
    }
  }
}

void midi_in_process_stream(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) process_byte(data[i]);
}

static void midi_in_task(void *pvParameters) {
  uint8_t rx_buf[RX_BUF_SIZE];
  while (1) {
    int len = uart_read_bytes(MIDI_NUM, rx_buf, RX_BUF_SIZE, UART_READ_TIMEOUT / portTICK_PERIOD_MS);
    if (len > 0) {
      // ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);
      midi_in_process_stream(rx_buf, len);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void midi_in_event_handler_init(void) {
  // Allocate initial SysEx buffer
  sys_ex_capacity = 256;
  sys_ex_buffer = malloc(sys_ex_capacity);
  if (!sys_ex_buffer) ESP_LOGE(TAG, "Failed to allocate initial SysEx buffer");
  
  xTaskCreate(midi_in_task, "midi_in_event", 4096, NULL, TASK_PRIORITY_MIDI_IN, NULL);
  ESP_LOGI(TAG, "MIDI IN event handler initialized");
}
