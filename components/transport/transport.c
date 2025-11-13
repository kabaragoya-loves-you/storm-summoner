#include "transport.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "midi_passthrough.h"
#include "tempo.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "TRANSPORT"

// State tracking
static transport_state_t s_state = TRANSPORT_STOPPED;
static SemaphoreHandle_t s_state_mutex = NULL;

// Position tracking (bar/beat)
static uint32_t s_current_bar = 1;     // Current bar number (1-based)
static uint8_t s_current_beat = 1;     // Current beat in bar (1-based)
static SemaphoreHandle_t s_position_mutex = NULL;

// Forward declarations
static void transport_event_handler(const event_t* event, void* context);
static void tempo_beat_handler(const event_t* event, void* context);

esp_err_t transport_init(void) {
  ESP_LOGI(TAG, "Initializing transport component");
  
  // Create mutex for thread-safe state access
  s_state_mutex = xSemaphoreCreateMutex();
  if (!s_state_mutex) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    return ESP_ERR_NO_MEM;
  }
  
  // Create mutex for position tracking
  s_position_mutex = xSemaphoreCreateMutex();
  if (!s_position_mutex) {
    ESP_LOGE(TAG, "Failed to create position mutex");
    return ESP_ERR_NO_MEM;
  }
  
  // Subscribe to transport events
  esp_err_t ret = event_bus_subscribe(EVENT_TRANSPORT_START, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to START event");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_TRANSPORT_STOP, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to STOP event");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_TRANSPORT_PAUSE, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to PAUSE event");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_TRANSPORT_CONTINUE, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to CONTINUE event");
    return ret;
  }
  
  ret = event_bus_subscribe(EVENT_TRANSPORT_RECORD, transport_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to RECORD event");
    return ret;
  }
  
  // Subscribe to tempo beat events for position tracking
  ret = event_bus_subscribe(EVENT_BEAT, tempo_beat_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to BEAT event");
    return ret;
  }
  
  ESP_LOGI(TAG, "Transport initialized");
  return ESP_OK;
}

static void set_state(transport_state_t new_state, transport_source_t source) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  
  if (s_state != new_state) {
    transport_state_t old_state = s_state;
    s_state = new_state;
    
    ESP_LOGI(TAG, "State changed: %d -> %d (source: %d)", old_state, new_state, source);
    
    // Reset position when starting playback from stopped state
    if (old_state == TRANSPORT_STOPPED && (new_state == TRANSPORT_PLAYING || new_state == TRANSPORT_RECORDING)) {
      xSemaphoreTake(s_position_mutex, portMAX_DELAY);
      s_current_bar = 1;
      s_current_beat = 1;
      xSemaphoreGive(s_position_mutex);
      ESP_LOGD(TAG, "Position reset to bar 1, beat 1");
    }
    
    // Post state change event
    event_t state_event = {
      .type = EVENT_TRANSPORT_STATE_CHANGED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.transport = {
        .state = new_state,
        .source = source
      }
    };
    
    // Release mutex before posting event to avoid deadlock
    xSemaphoreGive(s_state_mutex);
    event_bus_post(&state_event);
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

static void transport_event_handler(const event_t* event, void* context) {
  if (!event) return;
  
  // Extract source from event data (default to INTERNAL if not specified)
  transport_source_t source = event->data.transport.source;
  
  switch (event->type) {
    case EVENT_TRANSPORT_START:
      ESP_LOGD(TAG, "Received START event (source=%d)", source);
      set_state(TRANSPORT_PLAYING, source);
      // If source was MIDI and passthrough is enabled, don't echo (already forwarded)
      // If source was INTERNAL, MIDI was already sent by transport_play()
      // If source was MIDI and passthrough is OFF, we should echo
      if (source == TRANSPORT_SOURCE_MIDI) {
        // Check passthrough status
        extern bool midi_passthrough_usb_to_uart_is_enabled(void);
        extern bool midi_passthrough_uart_to_usb_is_enabled(void);
        // If passthrough is disabled, echo the message
        if (!midi_passthrough_usb_to_uart_is_enabled() && !midi_passthrough_uart_to_usb_is_enabled()) {
          send_start();
          send_mmc_play();
          ESP_LOGD(TAG, "Echoed MIDI Start/MMC Play (passthrough disabled)");
        }
      }
      break;
      
    case EVENT_TRANSPORT_STOP:
      ESP_LOGD(TAG, "Received STOP event (source=%d)", source);
      set_state(TRANSPORT_STOPPED, source);
      if (source == TRANSPORT_SOURCE_MIDI) {
        extern bool midi_passthrough_usb_to_uart_is_enabled(void);
        extern bool midi_passthrough_uart_to_usb_is_enabled(void);
        if (!midi_passthrough_usb_to_uart_is_enabled() && !midi_passthrough_uart_to_usb_is_enabled()) {
          send_stop();
          send_mmc_stop();
          ESP_LOGD(TAG, "Echoed MIDI Stop/MMC Stop (passthrough disabled)");
        }
      }
      break;
      
    case EVENT_TRANSPORT_PAUSE:
      ESP_LOGD(TAG, "Received PAUSE event (source=%d)", source);
      if (s_state == TRANSPORT_PLAYING || s_state == TRANSPORT_RECORDING) {
        set_state(TRANSPORT_PAUSED, source);
        if (source == TRANSPORT_SOURCE_MIDI) {
          extern bool midi_passthrough_usb_to_uart_is_enabled(void);
          extern bool midi_passthrough_uart_to_usb_is_enabled(void);
          if (!midi_passthrough_usb_to_uart_is_enabled() && !midi_passthrough_uart_to_usb_is_enabled()) {
            send_mmc_pause();
            ESP_LOGD(TAG, "Echoed MMC Pause (passthrough disabled)");
          }
        }
      }
      break;
      
    case EVENT_TRANSPORT_CONTINUE:
      ESP_LOGD(TAG, "Received CONTINUE event (source=%d)", source);
      if (s_state == TRANSPORT_PAUSED) {
        set_state(TRANSPORT_PLAYING, source);
        if (source == TRANSPORT_SOURCE_MIDI) {
          extern bool midi_passthrough_usb_to_uart_is_enabled(void);
          extern bool midi_passthrough_uart_to_usb_is_enabled(void);
          if (!midi_passthrough_usb_to_uart_is_enabled() && !midi_passthrough_uart_to_usb_is_enabled()) {
            send_continue();
            ESP_LOGD(TAG, "Echoed MIDI Continue (passthrough disabled)");
          }
        }
      }
      break;
      
    case EVENT_TRANSPORT_RECORD:
      ESP_LOGD(TAG, "Received RECORD event (source=%d)", source);
      set_state(TRANSPORT_RECORDING, source);
      if (source == TRANSPORT_SOURCE_MIDI) {
        extern bool midi_passthrough_usb_to_uart_is_enabled(void);
        extern bool midi_passthrough_uart_to_usb_is_enabled(void);
        if (!midi_passthrough_usb_to_uart_is_enabled() && !midi_passthrough_uart_to_usb_is_enabled()) {
          send_mmc_record_strobe();
          ESP_LOGD(TAG, "Echoed MMC Record Strobe (passthrough disabled)");
        }
      }
      break;
      
    default:
      break;
  }
}

transport_state_t transport_get_state(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  transport_state_t state = s_state;
  xSemaphoreGive(s_state_mutex);
  return state;
}

bool transport_is_playing(void) {
  transport_state_t state = transport_get_state();
  return (state == TRANSPORT_PLAYING || state == TRANSPORT_RECORDING);
}

bool transport_is_recording(void) {
  return (transport_get_state() == TRANSPORT_RECORDING);
}

// Transport control functions - send both MIDI Clock and MMC messages
esp_err_t transport_play(void) {
  // Send MIDI Clock Start
  send_start();
  
  // Send MMC Play
  send_mmc_play();
  
  // Post internal event tagged as INTERNAL source (already sent MIDI)
  event_t event = {
    .type = EVENT_TRANSPORT_START,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  ESP_LOGI(TAG, "Play (sent MIDI Start + MMC Play)");
  return event_bus_post(&event);
}

esp_err_t transport_stop(void) {
  // Send MIDI Clock Stop
  send_stop();
  
  // Send MMC Stop
  send_mmc_stop();
  
  // Post internal event tagged as INTERNAL source (already sent MIDI)
  event_t event = {
    .type = EVENT_TRANSPORT_STOP,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  ESP_LOGI(TAG, "Stop (sent MIDI Stop + MMC Stop)");
  return event_bus_post(&event);
}

esp_err_t transport_pause(void) {
  // Send MMC Pause
  send_mmc_pause();
  
  // Post internal event tagged as INTERNAL source (already sent MIDI)
  event_t event = {
    .type = EVENT_TRANSPORT_PAUSE,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  ESP_LOGI(TAG, "Pause (sent MMC Pause)");
  return event_bus_post(&event);
}

esp_err_t transport_record(void) {
  // Send MMC Record Strobe
  send_mmc_record_strobe();
  
  // Post internal event tagged as INTERNAL source (already sent MIDI)
  event_t event = {
    .type = EVENT_TRANSPORT_RECORD,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  ESP_LOGI(TAG, "Record (sent MMC Record Strobe)");
  return event_bus_post(&event);
}

esp_err_t transport_toggle(void) {
  transport_state_t current = transport_get_state();
  
  if (current == TRANSPORT_PLAYING || current == TRANSPORT_RECORDING) {
    ESP_LOGI(TAG, "Toggle: Stopping");
    return transport_stop();
  } else {
    ESP_LOGI(TAG, "Toggle: Playing");
    return transport_play();
  }
}

// Tempo beat event handler - advances position tracking
static void tempo_beat_handler(const event_t* event, void* context) {
  if (!event) return;
  
  // Only track position when playing or recording
  if (!transport_is_playing()) return;
  
  // Get current time signature from tempo
  time_signature_t sig = tempo_get_time_signature();
  
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  
  // Advance beat
  s_current_beat++;
  
  // Check if we've completed a bar
  if (s_current_beat > sig.numerator) {
    s_current_beat = 1;
    s_current_bar++;
  }
  
  xSemaphoreGive(s_position_mutex);
}

// Position tracking getters
uint32_t transport_get_current_bar(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  uint32_t bar = s_current_bar;
  xSemaphoreGive(s_position_mutex);
  return bar;
}

uint8_t transport_get_current_beat(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  uint8_t beat = s_current_beat;
  xSemaphoreGive(s_position_mutex);
  return beat;
}

void transport_reset_position(void) {
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  s_current_bar = 1;
  s_current_beat = 1;
  xSemaphoreGive(s_position_mutex);
  ESP_LOGI(TAG, "Position reset to bar 1, beat 1");
}
