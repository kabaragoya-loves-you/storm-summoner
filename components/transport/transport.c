#include "transport.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "TRANSPORT"

// State tracking
static transport_state_t s_state = TRANSPORT_STOPPED;
static SemaphoreHandle_t s_state_mutex = NULL;

// Forward declarations
static void transport_event_handler(const event_t* event, void* context);

esp_err_t transport_init(void) {
  ESP_LOGI(TAG, "Initializing transport component");
  
  // Create mutex for thread-safe state access
  s_state_mutex = xSemaphoreCreateMutex();
  if (!s_state_mutex) {
    ESP_LOGE(TAG, "Failed to create state mutex");
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
  
  ESP_LOGI(TAG, "Transport initialized");
  return ESP_OK;
}

static void set_state(transport_state_t new_state, transport_source_t source) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  
  if (s_state != new_state) {
    transport_state_t old_state = s_state;
    s_state = new_state;
    
    ESP_LOGI(TAG, "State changed: %d -> %d (source: %d)", old_state, new_state, source);
    
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
  
  transport_source_t source = TRANSPORT_SOURCE_INTERNAL;
  
  // Try to determine source from event data if available
  if (event->type == EVENT_TRANSPORT_START ||
      event->type == EVENT_TRANSPORT_STOP ||
      event->type == EVENT_TRANSPORT_PAUSE ||
      event->type == EVENT_TRANSPORT_CONTINUE ||
      event->type == EVENT_TRANSPORT_RECORD) {
    // These events might come from MIDI IN, UI, etc
    // For now, assume MIDI if not specified
    source = TRANSPORT_SOURCE_MIDI;
  }
  
  switch (event->type) {
    case EVENT_TRANSPORT_START:
      ESP_LOGD(TAG, "Received START event");
      set_state(TRANSPORT_PLAYING, source);
      break;
      
    case EVENT_TRANSPORT_STOP:
      ESP_LOGD(TAG, "Received STOP event");
      set_state(TRANSPORT_STOPPED, source);
      break;
      
    case EVENT_TRANSPORT_PAUSE:
      ESP_LOGD(TAG, "Received PAUSE event");
      if (s_state == TRANSPORT_PLAYING || s_state == TRANSPORT_RECORDING) {
        set_state(TRANSPORT_PAUSED, source);
      }
      break;
      
    case EVENT_TRANSPORT_CONTINUE:
      ESP_LOGD(TAG, "Received CONTINUE event");
      if (s_state == TRANSPORT_PAUSED) {
        set_state(TRANSPORT_PLAYING, source);
      }
      break;
      
    case EVENT_TRANSPORT_RECORD:
      ESP_LOGD(TAG, "Received RECORD event");
      set_state(TRANSPORT_RECORDING, source);
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
  
  // Post internal event
  event_t event = {
    .type = EVENT_TRANSPORT_START,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp()
  };
  ESP_LOGI(TAG, "Play (sent MIDI Start + MMC Play)");
  return event_bus_post(&event);
}

esp_err_t transport_stop(void) {
  // Send MIDI Clock Stop
  send_stop();
  
  // Send MMC Stop
  send_mmc_stop();
  
  // Post internal event
  event_t event = {
    .type = EVENT_TRANSPORT_STOP,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp()
  };
  ESP_LOGI(TAG, "Stop (sent MIDI Stop + MMC Stop)");
  return event_bus_post(&event);
}

esp_err_t transport_pause(void) {
  // Send MMC Pause
  send_mmc_pause();
  
  // Post internal event
  event_t event = {
    .type = EVENT_TRANSPORT_PAUSE,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp()
  };
  ESP_LOGI(TAG, "Pause (sent MMC Pause)");
  return event_bus_post(&event);
}

esp_err_t transport_record(void) {
  // Send MMC Record Strobe
  send_mmc_record_strobe();
  
  // Post internal event
  event_t event = {
    .type = EVENT_TRANSPORT_RECORD,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp()
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
