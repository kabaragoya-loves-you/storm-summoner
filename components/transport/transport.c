#include "transport.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "midi_passthrough.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TAG "TRANSPORT"

// State tracking
static transport_state_t s_state = TRANSPORT_STOPPED;
static SemaphoreHandle_t s_state_mutex = NULL;

// Position tracking (bar/beat)
static uint32_t s_current_bar = 1;     // Current bar number (1-based)
static uint8_t s_current_beat = 1;     // Current beat in bar (1-based)
static SemaphoreHandle_t s_position_mutex = NULL;

// Track last stop time to distinguish "Play while playing" from "Resume"
static uint32_t s_last_stop_time_ms = 0;
#define STOP_CONTINUE_WINDOW_MS 100  // If Continue arrives within 100ms of Stop, treat as "Play"

// Track pending "second stop" reset - deferred until we confirm no Continue is coming
static bool s_second_stop_pending = false;
static uint32_t s_second_stop_time_ms = 0;
#define SECOND_STOP_DEFER_MS 50  // Wait this long before confirming second stop reset

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
  ret = event_bus_subscribe_named(EVENT_BEAT, tempo_beat_handler, NULL,
    "transport.beat_pos");
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to subscribe to BEAT event");
    return ret;
  }
  
  ESP_LOGI(TAG, "Transport initialized");
  return ESP_OK;
}

static void set_state_ex(transport_state_t new_state, transport_source_t source, bool is_resume) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  
  if (s_state != new_state) {
    transport_state_t old_state = s_state;
    s_state = new_state;
    
    ESP_LOGI(TAG, "State changed: %d -> %d (source: %d, resume: %d)",
      old_state, new_state, source, is_resume);
    
    // Post state change event
    event_t state_event = {
      .type = EVENT_TRANSPORT_STATE_CHANGED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.transport = {
        .state = new_state,
        .source = source,
        .is_resume = is_resume ? 1 : 0
      }
    };
    
    // Release mutex before posting event to avoid deadlock
    xSemaphoreGive(s_state_mutex);
    event_bus_post(&state_event);
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

// Convenience wrapper for non-resume state changes
static void set_state(transport_state_t new_state, transport_source_t source) {
  set_state_ex(new_state, source, false);
}

static void transport_event_handler(const event_t* event, void* context) {
  if (!event) return;
  
  // Extract source from event data (default to INTERNAL if not specified)
  transport_source_t source = event->data.transport.source;
  
  switch (event->type) {
    case EVENT_TRANSPORT_START: {
      ESP_LOGD(TAG, "Received START event (source=%d)", source);
      // Clear any pending second-stop reset (START always resets anyway)
      s_second_stop_pending = false;
      // START means play from beginning - always reset position
      xSemaphoreTake(s_position_mutex, portMAX_DELAY);
      s_current_bar = 1;
      s_current_beat = 1;
      xSemaphoreGive(s_position_mutex);
      ESP_LOGD(TAG, "Position reset to bar 1, beat 1 (START)");
      
      // Check if we're already playing - if so, we need to force-notify
      // (set_state won't post event if state doesn't change)
      bool was_playing = transport_is_playing();
      set_state(TRANSPORT_PLAYING, source);
      
      // If already playing, force-post a state changed event so listeners restart
      if (was_playing) {
        ESP_LOGI(TAG, "Restarting from beginning (was already playing) - posting restart event");
        event_t restart_event = {
          .type = EVENT_TRANSPORT_STATE_CHANGED,
          .priority = EVENT_PRIORITY_HIGH,
          .timestamp = event_bus_get_current_timestamp(),
          .data.transport = {
            .state = TRANSPORT_PLAYING,
            .source = source
          }
        };
        esp_err_t err = event_bus_post(&restart_event);
        if (err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to post restart event: %s", esp_err_to_name(err));
        }
      }
      
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
    }
      
    case EVENT_TRANSPORT_STOP:
      ESP_LOGD(TAG, "Received STOP event (source=%d)", source);
      // If already stopped, this might be "second stop" (return to beginning)
      // BUT it could also be part of a Resume sequence (Stop+Continue from DAW)
      // Don't reset immediately - mark as pending and let Continue cancel it
      if (s_state == TRANSPORT_STOPPED) {
        s_second_stop_pending = true;
        s_second_stop_time_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGD(TAG, "Second STOP pending (waiting to see if Continue follows)");
      } else {
        // Only record stop time when actually transitioning from playing to stopped
        // This is used to detect Stop+Continue ("Play while playing") vs Resume
        s_last_stop_time_ms = (uint32_t)(esp_timer_get_time() / 1000);
        s_second_stop_pending = false;
      }
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
      
    case EVENT_TRANSPORT_CONTINUE: {
      ESP_LOGD(TAG, "Received CONTINUE event (source=%d)", source);
      
      // If there's a pending "second stop" reset, cancel it - this is a Resume, not "go to beginning"
      if (s_second_stop_pending) {
        ESP_LOGI(TAG, "Cancelled pending second-stop reset (this is a Resume)");
        s_second_stop_pending = false;
      }
      
      // Check if this Continue came shortly after a Stop (= "Play while playing")
      // vs a standalone Continue (= "Resume" which should keep current position)
      uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
      uint32_t since_stop = now_ms - s_last_stop_time_ms;
      bool is_resume = (since_stop >= STOP_CONTINUE_WINDOW_MS);
      
      if (!is_resume) {
        // Stop+Continue sequence = "Play while playing" → reset to beginning
        xSemaphoreTake(s_position_mutex, portMAX_DELAY);
        s_current_bar = 1;
        s_current_beat = 1;
        xSemaphoreGive(s_position_mutex);
        ESP_LOGI(TAG, "CONTINUE after STOP (%lums): reset to bar 1, beat 1",
          (unsigned long)since_stop);
      } else {
        // Standalone Continue = "Resume" → keep current position
        ESP_LOGI(TAG, "CONTINUE (Resume): keeping position bar %lu, beat %d",
          (unsigned long)s_current_bar, s_current_beat);
      }
      
      if (s_state == TRANSPORT_PAUSED || s_state == TRANSPORT_STOPPED) {
        set_state_ex(TRANSPORT_PLAYING, source, is_resume);
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
    }
      
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
// play() and record() are toggles
esp_err_t transport_play(void) {
  transport_state_t current = transport_get_state();
  
  // If already playing, pause
  if (current == TRANSPORT_PLAYING || current == TRANSPORT_RECORDING) {
    return transport_pause();
  }
  
  // Otherwise start playing
  if (current == TRANSPORT_PAUSED) {
    send_continue();
  } else {
    send_start();
  }
  send_mmc_play();
  
  event_t event = {
    .type = EVENT_TRANSPORT_START,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  ESP_LOGI(TAG, "Play toggle → playing (sent MIDI %s + MMC Play)",
    current == TRANSPORT_PAUSED ? "Continue" : "Start");
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
  transport_state_t current = transport_get_state();
  
  // Only pause if playing or recording
  if (current != TRANSPORT_PLAYING && current != TRANSPORT_RECORDING) {
    ESP_LOGD(TAG, "Pause ignored - not playing or recording");
    return ESP_OK;
  }
  
  send_mmc_pause();
  
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
  transport_state_t current = transport_get_state();
  
  // If already recording, pause
  if (current == TRANSPORT_RECORDING) {
    return transport_pause();
  }
  
  // Send appropriate MIDI clock message based on current state
  const char* midi_msg = NULL;
  if (current == TRANSPORT_PAUSED) {
    send_continue();
    midi_msg = "Continue";
  } else if (current == TRANSPORT_STOPPED) {
    send_start();
    midi_msg = "Start";
  }
  // If PLAYING, clock is already running - no MIDI clock message needed
  
  send_mmc_record_strobe();
  
  event_t event = {
    .type = EVENT_TRANSPORT_RECORD,
    .priority = EVENT_PRIORITY_HIGH,
    .timestamp = event_bus_get_current_timestamp(),
    .data.transport = {
      .source = TRANSPORT_SOURCE_INTERNAL
    }
  };
  
  if (midi_msg) {
    ESP_LOGI(TAG, "Record toggle → recording (sent MIDI %s + MMC Record Strobe)", midi_msg);
  } else {
    ESP_LOGI(TAG, "Record toggle → recording (sent MMC Record Strobe, clock already running)");
  }
  return event_bus_post(&event);
}

// Tempo beat event handler - syncs position with tempo
static void tempo_beat_handler(const event_t* event, void* context) {
  if (!event) return;
  
  // Only track position when playing or recording
  if (!transport_is_playing()) return;
  
  xSemaphoreTake(s_position_mutex, portMAX_DELAY);
  
  // Sync beat with tempo's beat counter from the event
  uint8_t beat_in_bar = event->data.beat.beat_in_bar;
  
  // If beat wrapped to 1, we've completed a bar
  if (beat_in_bar == 1 && s_current_beat > 1) {
    s_current_bar++;
  }
  
  s_current_beat = beat_in_bar;
  
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

bool transport_just_stopped(void) {
  uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
  uint32_t since_stop = now_ms - s_last_stop_time_ms;
  return since_stop < STOP_CONTINUE_WINDOW_MS;
}
