/**
 * MIDI IN Parser
 * 
 * Transport-agnostic MIDI message parser.
 * Parses MIDI byte stream and posts events to event bus.
 */

#include "midi_in_parser.h"
#include "midi_in.h"
// #include "midi_sysex_update.h" // Removed
#include "midi_identity.h"
#include "midi_passthrough.h"
#include "note_track_config.h"
#include "scene.h"
#include "transport.h"
#include "esp_log.h"
#include "event_bus.h"
#include "tempo.h"
#include <string.h>
#include <stdlib.h>

#define TAG "MIDI_PARSER"
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

// Current source for this parsing session
static uint8_t current_source = MIDI_SOURCE_UART;

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
      .source = current_source,  // Use current source instead of hardcoded
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
    
    // Post transport events for Start/Stop/Continue
    // Tag with TRANSPORT_SOURCE_MIDI so we don't echo if passthrough is enabled
    if (byte == 0xFA) { // Start
      ESP_LOGI(TAG, "MIDI START (0xFA) received");
      // CRITICAL: Reset tempo counters synchronously BEFORE posting event
      // This prevents clock ticks from racing ahead while event is queued
      tempo_midi_transport_start();
      
      event_t transport_event = {
        .type = EVENT_TRANSPORT_START,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.transport = {
          .source = TRANSPORT_SOURCE_MIDI
        }
      };
      event_bus_post(&transport_event);
    }
    else if (byte == 0xFC) { // Stop
      ESP_LOGI(TAG, "MIDI STOP (0xFC) received");
      event_t transport_event = {
        .type = EVENT_TRANSPORT_STOP,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.transport = {
          .source = TRANSPORT_SOURCE_MIDI
        }
      };
      event_bus_post(&transport_event);
    }
    else if (byte == 0xFB) { // Continue
      ESP_LOGI(TAG, "MIDI CONTINUE (0xFB) received (resume)");
      event_t transport_event = {
        .type = EVENT_TRANSPORT_CONTINUE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.transport = {
          .source = TRANSPORT_SOURCE_MIDI
        }
      };
      event_bus_post(&transport_event);
    }
    
    // For realtime clock, notify tempo if in MIDI clock mode
    if (byte == 0xF8) {
      tempo_midi_clock_tick();
    }
    
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
      // Complete SysEx message
      
      // Check if it's an identity request and handle it
      midi_identity_handle_request(sys_ex_buffer, sys_ex_index);
      
      // If not handled by identity handler, post as general SysEx event
      // SysEx firmware update functionality has been removed in favor of CDC
      {
        uint8_t* sysex_copy = malloc(sys_ex_index);
        if (sysex_copy) {
          memcpy(sysex_copy, sys_ex_buffer, sys_ex_index);
          post_midi_event(MIDI_EVENT_SYS_EX, 0, 0, 0, 0xF0, sysex_copy, sys_ex_index);
        } else {
          ESP_LOGE(TAG, "Failed to allocate SysEx copy");
        }
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

      if (sys_common_status == 0xF2) {
        uint16_t spp = (uint16_t)data1 | ((uint16_t)data2 << 7);
        ESP_LOGI(TAG, "MIDI SPP (0xF2) received: %u sixteenths", (unsigned)spp);
        transport_set_song_position(spp);
      }

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

void midi_in_process_stream(const uint8_t *data, size_t len, uint8_t source) {
  // Set the current source for this parsing session
  current_source = source;

  // When the active scene's Note Track is enabled and the global filter mode
  // is KILL, we cannot raw-forward the byte chunk. Use the per-message
  // filtered path instead so we can drop note bytes selectively while
  // preserving every other message.
  scene_t* cur_scene = scene_get_current();
  bool kill_active = cur_scene && cur_scene->note_track.enabled
    && note_track_get_filter_mode() == NOTE_TRACK_FILTER_KILL;
  if (kill_active) {
    midi_passthrough_forward_filtered(source, data, len);
  } else if (source == MIDI_SOURCE_UART) {
    midi_passthrough_forward_from_uart(data, len);
  } else if (source == MIDI_SOURCE_USB) {
    midi_passthrough_forward_from_usb(data, len);
  }

  // Process the MIDI data
  for (size_t i = 0; i < len; i++) process_byte(data[i]);
}

void midi_in_parser_init(void) {
  // Allocate initial SysEx buffer
  sys_ex_capacity = 256;
  sys_ex_buffer = malloc(sys_ex_capacity);
  if (!sys_ex_buffer) {
    ESP_LOGE(TAG, "Failed to allocate initial SysEx buffer");
  }
  
  // SysEx update handler has been removed
  // midi_sysex_update_init();
  
  ESP_LOGI(TAG, "MIDI parser initialized");
}
