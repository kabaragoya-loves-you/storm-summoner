/**
 * MIDI IN Debug Handler
 * 
 * Logs all incoming MIDI messages with source interface information.
 * Useful for testing and debugging MIDI IN functionality.
 */

#include "midi_in_debug.h"
#include "midi_in.h"
#include "event_bus.h"
#include "app_settings.h"
#include "esp_log.h"
#include <stdlib.h>

#define TAG "MIDI_IN_DEBUG"
#define NVS_KEY_MIDI_IN_DEBUG "midi_in_dbg"

static bool s_initialized = false;

// Forward declaration
static void midi_in_debug_handler(const event_t* event, void* context);

void midi_in_debug_init(void) {
  // Load debug setting from NVS
  uint8_t debug_enabled = 0;
  if (app_settings_load_u8(NVS_KEY_MIDI_IN_DEBUG, &debug_enabled) == ESP_OK && debug_enabled) {
    // Subscribe to MIDI IN events
    event_bus_subscribe(EVENT_MIDI_IN, midi_in_debug_handler, NULL);
    s_initialized = true;
    ESP_LOGI(TAG, "MIDI IN debug logging enabled (from NVS)");
  }
}

// Helper to get source name
static const char* get_source_name(midi_source_t source) {
  switch (source) {
    case MIDI_SOURCE_UART: return "UART";
    case MIDI_SOURCE_USB: return "USB";
    case MIDI_SOURCE_NETWORK: return "NETWORK";
    case MIDI_SOURCE_INTERNAL: return "INTERNAL";
    default: return "UNKNOWN";
  }
}

// Helper to get event type name
static const char* get_event_type_name(uint8_t type) {
  switch (type) {
    case MIDI_EVENT_NOTE_OFF: return "Note Off";
    case MIDI_EVENT_NOTE_ON: return "Note On";
    case MIDI_EVENT_POLY_AFTERTOUCH: return "Poly AT";
    case MIDI_EVENT_CONTROL_CHANGE: return "CC";
    case MIDI_EVENT_PROGRAM_CHANGE: return "Prog Change";
    case MIDI_EVENT_CHANNEL_AFTERTOUCH: return "Chan AT";
    case MIDI_EVENT_PITCH_BEND: return "Pitch Bend";
    case MIDI_EVENT_TIME_CODE: return "Time Code";
    case MIDI_EVENT_SONG_POSITION: return "Song Pos";
    case MIDI_EVENT_SONG_SELECT: return "Song Sel";
    case MIDI_EVENT_TUNE_REQUEST: return "Tune Req";
    case MIDI_EVENT_SYS_EX: return "SysEx";
    case MIDI_EVENT_REALTIME_CLOCK: return "Clock";
    case MIDI_EVENT_REALTIME_TICK: return "Tick";
    case MIDI_EVENT_REALTIME_START: return "Start";
    case MIDI_EVENT_REALTIME_CONTINUE: return "Continue";
    case MIDI_EVENT_REALTIME_STOP: return "Stop";
    case MIDI_EVENT_REALTIME_RESET: return "Reset";
    case MIDI_EVENT_ACTIVE_SENSING: return "Active Sense";
    default: return "Unknown";
  }
}

static void midi_in_debug_handler(const event_t* event, void* context) {
  if (event->type != EVENT_MIDI_IN) return;
  
  uint8_t type = event->data.midi_in.type;
  uint8_t channel = event->data.midi_in.channel;
  uint8_t data1 = event->data.midi_in.data1;
  uint8_t data2 = event->data.midi_in.data2;
  uint8_t source = event->data.midi_in.source;
  uint16_t length = event->data.midi_in.length;
  
  const char* source_name = get_source_name(source);
  const char* type_name = get_event_type_name(type);
  
  // Log based on message type
  switch (type) {
    case MIDI_EVENT_NOTE_ON:
    case MIDI_EVENT_NOTE_OFF:
      ESP_LOGI(TAG, "[%s] %s: Ch=%d Note=%d Vel=%d", 
        source_name, type_name, channel + 1, data1, data2);
      break;
      
    case MIDI_EVENT_CONTROL_CHANGE:
      ESP_LOGI(TAG, "[%s] %s: Ch=%d CC=%d Val=%d", 
        source_name, type_name, channel + 1, data1, data2);
      break;
      
    case MIDI_EVENT_PROGRAM_CHANGE:
      ESP_LOGI(TAG, "[%s] %s: Ch=%d Prog=%d", 
        source_name, type_name, channel + 1, data1);
      break;
      
    case MIDI_EVENT_POLY_AFTERTOUCH:
      ESP_LOGI(TAG, "[%s] %s: Ch=%d Note=%d Pressure=%d", 
        source_name, type_name, channel + 1, data1, data2);
      break;
      
    case MIDI_EVENT_CHANNEL_AFTERTOUCH:
      ESP_LOGI(TAG, "[%s] %s: Ch=%d Pressure=%d", 
        source_name, type_name, channel + 1, data1);
      break;
      
    case MIDI_EVENT_PITCH_BEND:
      {
        int16_t bend = ((data2 << 7) | data1) - 8192;
        ESP_LOGI(TAG, "[%s] %s: Ch=%d Value=%d", 
          source_name, type_name, channel + 1, bend);
      }
      break;
      
    case MIDI_EVENT_SYS_EX:
      ESP_LOGI(TAG, "[%s] %s: %d bytes", source_name, type_name, length);
      if (event->data.midi_in.sysex_data && length > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, event->data.midi_in.sysex_data, 
          length > 32 ? 32 : length, ESP_LOG_INFO);
      }
      break;
      
    case MIDI_EVENT_REALTIME_CLOCK:
      // Too frequent, only log at DEBUG level
      ESP_LOGD(TAG, "[%s] Clock", source_name);
      break;
      
    case MIDI_EVENT_REALTIME_START:
    case MIDI_EVENT_REALTIME_STOP:
    case MIDI_EVENT_REALTIME_CONTINUE:
      ESP_LOGI(TAG, "[%s] %s", source_name, type_name);
      break;
      
    case MIDI_EVENT_TIME_CODE:
      ESP_LOGI(TAG, "[%s] %s: Type=%d Val=%d", 
        source_name, type_name, (data1 >> 4) & 0x07, data1 & 0x0F);
      break;
      
    case MIDI_EVENT_SONG_POSITION:
      {
        uint16_t position = (data2 << 7) | data1;
        ESP_LOGI(TAG, "[%s] %s: %d", source_name, type_name, position);
      }
      break;
      
    case MIDI_EVENT_SONG_SELECT:
      ESP_LOGI(TAG, "[%s] %s: Song=%d", source_name, type_name, data1);
      break;
      
    case MIDI_EVENT_TUNE_REQUEST:
    case MIDI_EVENT_REALTIME_RESET:
    case MIDI_EVENT_ACTIVE_SENSING:
      ESP_LOGI(TAG, "[%s] %s", source_name, type_name);
      break;
      
    default:
      ESP_LOGI(TAG, "[%s] Unknown MIDI event: type=%d", source_name, type);
      break;
  }
  
  // Free SysEx data if present (we're responsible for cleanup)
  if (event->data.midi_in.sysex_data) {
    free(event->data.midi_in.sysex_data);
  }
}

void midi_in_debug_enable(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "MIDI IN debug already enabled");
    return;
  }
  
  // Subscribe to MIDI IN events
  event_bus_subscribe(EVENT_MIDI_IN, midi_in_debug_handler, NULL);
  
  s_initialized = true;
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_MIDI_IN_DEBUG, 1);
  
  ESP_LOGI(TAG, "MIDI IN debug logging enabled");
}

void midi_in_debug_disable(void) {
  if (!s_initialized) {
    return;
  }
  
  // Unsubscribe from MIDI IN events
  event_bus_unsubscribe(EVENT_MIDI_IN, midi_in_debug_handler);
  
  s_initialized = false;
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_MIDI_IN_DEBUG, 0);
  
  ESP_LOGI(TAG, "MIDI IN debug logging disabled");
}

bool midi_in_debug_is_enabled(void) {
  return s_initialized;
}

