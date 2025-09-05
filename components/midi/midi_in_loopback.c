/**
 * MIDI IN Loopback Example
 * 
 * This demonstrates how to use the event-based MIDI IN system to create
 * a simple MIDI loopback/echo that forwards all incoming MIDI messages
 * to MIDI OUT.
 * 
 * This example shows:
 * 1. How to subscribe to MIDI IN events
 * 2. How to filter and process different MIDI message types
 * 3. How to forward messages to MIDI OUT
 * 4. Proper handling of SysEx data
 * 
 * To use this in your application:
 * - Call midi_loopback_init() to start the loopback
 * - Call midi_loopback_stop() to stop it
 * - Modify the filtering logic as needed for your use case
 */

#include "event_bus.h"
#include "midi_in.h"
#include "midi_messages.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "MIDI_LOOPBACK"

// Optional: Track statistics
static struct {
  uint32_t notes_forwarded;
  uint32_t cc_forwarded;
  uint32_t sysex_forwarded;
  uint32_t clock_forwarded;
  uint32_t total_forwarded;
} loopback_stats = {0};

// Configuration options
static struct {
  bool forward_notes;
  bool forward_cc;
  bool forward_program_change;
  bool forward_realtime;
  bool forward_sysex;
  bool filter_active_sensing;
  uint8_t channel_filter;  // 0xFF = all channels, 0-15 = specific channel
} loopback_config = {
  .forward_notes = true,
  .forward_cc = true,
  .forward_program_change = true,
  .forward_realtime = true,
  .forward_sysex = true,
  .filter_active_sensing = true,  // Usually want to filter these
  .channel_filter = 0xFF          // Forward all channels
};

static void midi_loopback_handle_event(const event_t* event, void* context) {
  if (event->type != EVENT_MIDI_IN) return;
  
  // Extract MIDI data
  uint8_t type = event->data.midi_in.type;
  uint8_t channel = event->data.midi_in.channel;
  uint8_t data1 = event->data.midi_in.data1;
  uint8_t data2 = event->data.midi_in.data2;
  
  // Check channel filter
  if (loopback_config.channel_filter != 0xFF && 
    channel != loopback_config.channel_filter) {
    // Skip messages not on our filtered channel
    // (except for non-channel messages)
    if (type <= MIDI_EVENT_PITCH_BEND) return;
  }
  
  // Process based on message type
  switch (type) {
    case MIDI_EVENT_NOTE_OFF:
    case MIDI_EVENT_NOTE_ON:
      if (loopback_config.forward_notes) {
        if (type == MIDI_EVENT_NOTE_ON) {
          send_note_on(channel, data1, data2);
        } else {
          send_note_off(channel, data1, data2);
        }
        loopback_stats.notes_forwarded++;
        loopback_stats.total_forwarded++;
        ESP_LOGD(TAG, "Forwarded Note %s ch=%d note=%d vel=%d", type == MIDI_EVENT_NOTE_ON ? "ON" : "OFF", channel, data1, data2);
      }
      break;
      
    case MIDI_EVENT_CONTROL_CHANGE:
      if (loopback_config.forward_cc) {
        send_control_change(channel, data1, data2);
        loopback_stats.cc_forwarded++;
        loopback_stats.total_forwarded++;
        ESP_LOGD(TAG, "Forwarded CC ch=%d cc=%d val=%d", channel, data1, data2);
      }
      break;
      
    case MIDI_EVENT_PROGRAM_CHANGE:
      if (loopback_config.forward_program_change) {
        send_program_change(channel, data1);
        loopback_stats.total_forwarded++;
        ESP_LOGD(TAG, "Forwarded Program Change ch=%d prog=%d", channel, data1);
      }
      break;
      
    case MIDI_EVENT_PITCH_BEND:
      if (loopback_config.forward_notes) {  // Group with notes
        // Convert back to signed pitch bend value
        int16_t bend_value = (data2 << 7) | data1;
        bend_value -= 8192;  // Center at 0
        send_pitch_bend(channel, bend_value);
        loopback_stats.total_forwarded++;
        ESP_LOGD(TAG, "Forwarded Pitch Bend ch=%d val=%d", channel, bend_value);
      }
      break;
      
    case MIDI_EVENT_CHANNEL_AFTERTOUCH:
      if (loopback_config.forward_notes) {  // Group with notes
        send_channel_aftertouch(channel, data1);
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_POLY_AFTERTOUCH:
      if (loopback_config.forward_notes) {  // Group with notes
        send_poly_aftertouch(channel, data1, data2);
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_REALTIME_CLOCK:
      if (loopback_config.forward_realtime) {
        send_clock();
        loopback_stats.clock_forwarded++;
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_REALTIME_START:
      if (loopback_config.forward_realtime) {
        send_start();
        loopback_stats.total_forwarded++;
        ESP_LOGI(TAG, "Forwarded Start");
      }
      break;
      
    case MIDI_EVENT_REALTIME_STOP:
      if (loopback_config.forward_realtime) {
        send_stop();
        loopback_stats.total_forwarded++;
        ESP_LOGI(TAG, "Forwarded Stop");
      }
      break;
      
    case MIDI_EVENT_REALTIME_CONTINUE:
      if (loopback_config.forward_realtime) {
        send_continue();
        loopback_stats.total_forwarded++;
        ESP_LOGI(TAG, "Forwarded Continue");
      }
      break;
      
    case MIDI_EVENT_ACTIVE_SENSING:
      if (loopback_config.forward_realtime && 
        !loopback_config.filter_active_sensing) {
        send_active_sensing();
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_SYS_EX:
      if (loopback_config.forward_sysex && event->data.midi_in.sysex_data) {
        // SysEx requires special handling
        // The data includes F0 and F7, so we can send it directly
        uint8_t* sysex = event->data.midi_in.sysex_data;
        uint16_t len = event->data.midi_in.length;
        
        // Validate it's a proper SysEx message
        if (len >= 2 && sysex[0] == 0xF0 && sysex[len-1] == 0xF7) {
          // Extract the data between F0 and F7
          if (len > 2) {
            send_sysex(sysex + 1, len - 2);
            loopback_stats.sysex_forwarded++;
            loopback_stats.total_forwarded++;
            ESP_LOGI(TAG, "Forwarded SysEx (%d bytes)", len);
          }
        } else {
          ESP_LOGW(TAG, "Invalid SysEx format");
        }
      }
      break;
      
    case MIDI_EVENT_TIME_CODE:
      if (loopback_config.forward_realtime) {
        // MTC Quarter Frame: data1 contains the quarter frame data
        send_time_code((data1 >> 4) & 0x07, data1 & 0x0F);
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_SONG_POSITION:
      if (loopback_config.forward_realtime) {
        uint16_t position = (data2 << 7) | data1;
        send_song_position(position);
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_SONG_SELECT:
      if (loopback_config.forward_realtime) {
        send_song_select(data1);
        loopback_stats.total_forwarded++;
      }
      break;
      
    case MIDI_EVENT_TUNE_REQUEST:
      if (loopback_config.forward_realtime) {
        send_tune_request();
        loopback_stats.total_forwarded++;
      }
      break;
      
    default:
      ESP_LOGW(TAG, "Unknown MIDI event type: %d", type);
      break;
  }
  
  // Free SysEx data if present (we're responsible for cleanup)
  if (event->data.midi_in.sysex_data) free(event->data.midi_in.sysex_data);
}

// Public API

void midi_loopback_init(void) {
  // Subscribe to MIDI IN events
  event_bus_subscribe(EVENT_MIDI_IN, midi_loopback_handle_event, NULL);
  
  // Reset statistics
  loopback_stats.notes_forwarded = 0;
  loopback_stats.cc_forwarded = 0;
  loopback_stats.sysex_forwarded = 0;
  loopback_stats.clock_forwarded = 0;
  loopback_stats.total_forwarded = 0;
  
  ESP_LOGI(TAG, "MIDI loopback initialized");
}

void midi_loopback_stop(void) {
  // Unsubscribe from events
  event_bus_unsubscribe(EVENT_MIDI_IN, midi_loopback_handle_event);
  
  ESP_LOGI(TAG, "MIDI loopback stopped. Stats: total=%lu, notes=%lu, cc=%lu, sysex=%lu, clock=%lu",
    loopback_stats.total_forwarded,
    loopback_stats.notes_forwarded,
    loopback_stats.cc_forwarded,
    loopback_stats.sysex_forwarded,
    loopback_stats.clock_forwarded);
}

// Configuration functions

void midi_loopback_set_channel_filter(uint8_t channel) {
  if (channel > 15 && channel != 0xFF) {
    ESP_LOGW(TAG, "Invalid channel filter %d", channel);
    return;
  }
  loopback_config.channel_filter = channel;
  ESP_LOGI(TAG, "Channel filter set to %s", channel == 0xFF ? "ALL" : "specific channel");
}

void midi_loopback_set_note_forwarding(bool enable) {
  loopback_config.forward_notes = enable;
}

void midi_loopback_set_cc_forwarding(bool enable) {
  loopback_config.forward_cc = enable;
}

void midi_loopback_set_realtime_forwarding(bool enable) {
  loopback_config.forward_realtime = enable;
}

void midi_loopback_set_sysex_forwarding(bool enable) {
  loopback_config.forward_sysex = enable;
}

void midi_loopback_set_active_sensing_filter(bool filter) {
  loopback_config.filter_active_sensing = filter;
}

// Get statistics
void midi_loopback_get_stats(uint32_t* total, uint32_t* notes, uint32_t* cc, uint32_t* sysex, uint32_t* clock) {
  if (total) *total = loopback_stats.total_forwarded;
  if (notes) *notes = loopback_stats.notes_forwarded;
  if (cc) *cc = loopback_stats.cc_forwarded;
  if (sysex) *sysex = loopback_stats.sysex_forwarded;
  if (clock) *clock = loopback_stats.clock_forwarded;
}
