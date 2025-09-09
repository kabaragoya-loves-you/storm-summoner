#include "tempo.h"
#include "transport.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "app_settings.h"
#include "task_priorities.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "TEMPO"
#define NVS_KEY_BPM "tempo_bpm"
#define NVS_KEY_LED_SYNC "tempo_led_sync"
#define NVS_KEY_TIME_SIG_NUM "tempo_ts_num"
#define NVS_KEY_TIME_SIG_DEN "tempo_ts_den"

// Constants
#define MIN_BPM 30
#define MAX_BPM 250
#define DEFAULT_BPM 120
#define MIDI_CLOCKS_PER_QUARTER 24

// State variables
static uint8_t s_bpm = DEFAULT_BPM;
static tempo_clock_source_t s_clock_source = CLOCK_SOURCE_INTERNAL;
static time_signature_t s_time_signature = {4, 4};  // Default 4/4
static bool s_led_sync_enabled = false;
static tempo_note_divider_t s_note_divider = DIVIDER_QUARTER;

// Task and timing
static TaskHandle_t s_tempo_task_handle = NULL;
static SemaphoreHandle_t s_state_mutex = NULL;
static uint32_t s_tick_counter = 0;
static uint8_t s_beat_counter = 0;  // Counts beats within bar

// Tap tempo
#define TAP_BUFFER_SIZE 4
#define TAP_TIMEOUT_MS 2000
static uint32_t s_tap_timestamps[TAP_BUFFER_SIZE] = {0};
static int s_tap_count = 0;
static int s_tap_index = 0;

// External sync tracking
static uint32_t s_last_sync_tick_ms = 0;
static SemaphoreHandle_t s_sync_semaphore = NULL;

// Forward declarations
static void tempo_task(void *pvParameters);
static void transport_state_handler(const event_t* event, void* context);
static void publish_beat_event(void);
static void publish_tempo_changed_event(void);

void tempo_init(void) {
  ESP_LOGI(TAG, "Initializing tempo component");
  
  // Create mutex
  s_state_mutex = xSemaphoreCreateMutex();
  if (!s_state_mutex) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    return;
  }
  
  // Create sync semaphore
  s_sync_semaphore = xSemaphoreCreateBinary();
  if (!s_sync_semaphore) {
    ESP_LOGE(TAG, "Failed to create sync semaphore");
    return;
  }
  
  // Load settings from NVS
  uint32_t bpm = DEFAULT_BPM;
  if (app_settings_load_u32(NVS_KEY_BPM, &bpm) == ESP_OK) {
    s_bpm = (uint8_t)(bpm > MAX_BPM ? MAX_BPM : (bpm < MIN_BPM ? MIN_BPM : bpm));
  }
  
  uint8_t led_sync = 0;
  if (app_settings_load_u8(NVS_KEY_LED_SYNC, &led_sync) == ESP_OK) s_led_sync_enabled = (led_sync != 0);
  
  uint8_t ts_num = 4, ts_den = 4;
  app_settings_load_u8(NVS_KEY_TIME_SIG_NUM, &ts_num);
  app_settings_load_u8(NVS_KEY_TIME_SIG_DEN, &ts_den);
  s_time_signature.numerator = ts_num;
  s_time_signature.denominator = ts_den;
  
  // Subscribe to transport state changes
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);
  
  ESP_LOGI(TAG, "Tempo initialized - BPM: %d, Time Sig: %d/%d, LED Sync: %s",
    s_bpm, s_time_signature.numerator, s_time_signature.denominator,
    s_led_sync_enabled ? "ON" : "OFF");
}

static void tempo_task(void *pvParameters) {
  ESP_LOGI(TAG, "Tempo task started");
  
  TickType_t last_wake_time = xTaskGetTickCount();
  uint8_t last_bpm = s_bpm;
  
  while (1) {
    // Check if we should be running
    if (!transport_is_playing()) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint8_t current_bpm = s_bpm;
    tempo_clock_source_t source = s_clock_source;
    xSemaphoreGive(s_state_mutex);
    
    if (source == CLOCK_SOURCE_INTERNAL) {
      // Calculate tick interval
      uint32_t tick_interval_ms = 60000 / (MIDI_CLOCKS_PER_QUARTER * current_bpm);
      
      // Send MIDI clock directly (low latency requirement)
      send_clock();
      
      // Track ticks and beats
      s_tick_counter++;
      
      // Check if we've completed a beat (based on divider)
      if (s_tick_counter % s_note_divider == 0) {
        s_beat_counter++;
        if (s_beat_counter > s_time_signature.numerator) s_beat_counter = 1;
        publish_beat_event();
      }
      
      // Check if BPM changed
      if (current_bpm != last_bpm) {
        last_bpm = current_bpm;
        publish_tempo_changed_event();
      }
      
      // Delay until next tick
      vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(tick_interval_ms));
    }
    else if (source == CLOCK_SOURCE_SYNC) {
      // Wait for sync pulse
      if (xSemaphoreTake(s_sync_semaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Calculate BPM from sync interval
        uint32_t now = esp_timer_get_time() / 1000;
        if (s_last_sync_tick_ms > 0) {
          uint32_t interval_ms = now - s_last_sync_tick_ms;
          if (interval_ms > 0) {
            uint8_t new_bpm = (uint8_t)(60000 / interval_ms);
            if (new_bpm >= MIN_BPM && new_bpm <= MAX_BPM && new_bpm != s_bpm) {
              xSemaphoreTake(s_state_mutex, portMAX_DELAY);
              s_bpm = new_bpm;
              xSemaphoreGive(s_state_mutex);
              publish_tempo_changed_event();
            }
          }
        }
        s_last_sync_tick_ms = now;
        
        // Generate clocks for this beat
        for (int i = 0; i < MIDI_CLOCKS_PER_QUARTER; i++) {
          send_clock();
          vTaskDelay(pdMS_TO_TICKS(1)); // Small delay between clocks
        }
        
        s_beat_counter++;
        if (s_beat_counter > s_time_signature.numerator) s_beat_counter = 1;
        publish_beat_event();
      }
    }
    else { // CLOCK_SOURCE_MIDI
      // In MIDI clock mode, we don't generate clocks, just track them
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

static void transport_state_handler(const event_t* event, void* context) {
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  
  transport_state_t state = event->data.transport.state;
  
  switch (state) {
    case TRANSPORT_PLAYING:
    case TRANSPORT_RECORDING:
      tempo_start();
      break;
      
    case TRANSPORT_STOPPED:
      tempo_stop();
      // Reset beat counter
      s_beat_counter = 0;
      s_tick_counter = 0;
      break;
      
    case TRANSPORT_PAUSED:
      tempo_stop();
      // Don't reset counters - maintain position
      break;
  }
}

static void publish_beat_event(void) {
  // Publish beat event
  event_t beat_event = {
    .type = EVENT_BEAT,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.beat = {
      .beat_in_bar = s_beat_counter,
      .bar_length = s_time_signature.numerator
    }
  };
  event_bus_post(&beat_event);
  
  // Handle LED sync if enabled
  if (s_led_sync_enabled) {
    uint32_t duration_ms;
    
    // Calculate beat duration
    uint32_t beat_duration_ms = 60000 / s_bpm;
    
    if (s_beat_counter == 1) {
      // Downbeat - longer flash
      duration_ms = beat_duration_ms / 3;  // 33% duty cycle
    } else {
      // Regular beat
      duration_ms = beat_duration_ms / 6;  // 16% duty cycle  
    }
    
    // Request LED flash
    event_t led_event = {
      .type = EVENT_LED_FLASH_REQUEST,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp(),
      .data.led_flash = {
        .duration_ms = duration_ms
      }
    };
    event_bus_post(&led_event);
  }
}

static void publish_tempo_changed_event(void) {
  event_t tempo_event = {
    .type = EVENT_TEMPO_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.tempo = {
      .bpm = s_bpm
    }
  };
  event_bus_post(&tempo_event);
  
  ESP_LOGI(TAG, "Tempo changed to %d BPM", s_bpm);
}

// Public API functions
void tempo_start(void) {
  if (s_tempo_task_handle == NULL) {
    BaseType_t ret = xTaskCreate(tempo_task, "tempo", 3072, NULL, TASK_PRIORITY_MIDI_TEMPO, &s_tempo_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create tempo task");
    } else {
      ESP_LOGI(TAG, "Tempo started");
      // Publish initial tempo
      publish_tempo_changed_event();
    }
  }
}

void tempo_stop(void) {
  if (s_tempo_task_handle != NULL) {
    vTaskDelete(s_tempo_task_handle);
    s_tempo_task_handle = NULL;
    ESP_LOGI(TAG, "Tempo stopped");
  }
}

void tempo_set_bpm(uint16_t bpm) {
  if (bpm < MIN_BPM) bpm = MIN_BPM;
  if (bpm > MAX_BPM) bpm = MAX_BPM;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_bpm != (uint8_t)bpm) {
    s_bpm = (uint8_t)bpm;
    xSemaphoreGive(s_state_mutex);
    
    // Save to NVS
    app_settings_save_u32(NVS_KEY_BPM, bpm);
    
    // Notify about change
    publish_tempo_changed_event();
  } else {
    xSemaphoreGive(s_state_mutex);
  }
}

uint16_t tempo_get_bpm(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint16_t bpm = s_bpm;
  xSemaphoreGive(s_state_mutex);
  return bpm;
}

void tempo_set_source(tempo_clock_source_t source) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_source = source;
  xSemaphoreGive(s_state_mutex);
  
  ESP_LOGI(TAG, "Clock source set to %d", source);
}

void tempo_sync_pulse(void) {
  xSemaphoreGive(s_sync_semaphore);
}

void tempo_enable_quarter_note_log(bool enable) {
  ESP_LOGI(TAG, "Quarter note logging %s (now uses beat events)", enable ? "enabled" : "disabled");
}

void tempo_tap_event(void) {
  uint32_t now = esp_timer_get_time() / 1000;
  
  // Check for timeout
  if (s_tap_count > 0 && (now - s_tap_timestamps[(s_tap_index - 1 + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE]) > TAP_TIMEOUT_MS) {
    s_tap_count = 0;
    s_tap_index = 0;
  }
  
  // Store timestamp
  s_tap_timestamps[s_tap_index] = now;
  s_tap_index = (s_tap_index + 1) % TAP_BUFFER_SIZE;
  if (s_tap_count < TAP_BUFFER_SIZE) s_tap_count++;
  
  // Calculate BPM if we have enough taps
  if (s_tap_count >= 2) {
    uint32_t total_interval = 0;
    int intervals = 0;
    
    for (int i = 1; i < s_tap_count; i++) {
      int idx1 = (s_tap_index - i - 1 + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE;
      int idx2 = (s_tap_index - i + TAP_BUFFER_SIZE) % TAP_BUFFER_SIZE;
      total_interval += s_tap_timestamps[idx2] - s_tap_timestamps[idx1];
      intervals++;
    }
    
    if (intervals > 0 && total_interval > 0) {
      uint32_t avg_interval = total_interval / intervals;
      uint16_t new_bpm = 60000 / avg_interval;
      tempo_set_bpm(new_bpm);
    }
  }
}

void tempo_midi_clock_tick(void) {
  if (s_clock_source != CLOCK_SOURCE_MIDI) return;
  
  static uint32_t midi_tick_count = 0;
  static uint32_t last_beat_time = 0;
  
  midi_tick_count++;
  
  // Check for beat
  if (midi_tick_count % s_note_divider == 0) {
    uint32_t now = esp_timer_get_time() / 1000;
    
    // Calculate BPM from MIDI clock
    if (last_beat_time > 0) {
      uint32_t interval = now - last_beat_time;
      if (interval > 0) {
        uint8_t new_bpm = (uint8_t)(60000 / interval);
        if (new_bpm >= MIN_BPM && new_bpm <= MAX_BPM && new_bpm != s_bpm) {
          xSemaphoreTake(s_state_mutex, portMAX_DELAY);
          s_bpm = new_bpm;
          xSemaphoreGive(s_state_mutex);
          publish_tempo_changed_event();
        }
      }
    }
    last_beat_time = now;
    
    s_beat_counter++;
    if (s_beat_counter > s_time_signature.numerator) {
      s_beat_counter = 1;
    }
    publish_beat_event();
  }
}

void tempo_set_note_divider(tempo_note_divider_t divider) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_note_divider = divider;
  xSemaphoreGive(s_state_mutex);
}

tempo_note_divider_t tempo_get_note_divider(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_note_divider_t divider = s_note_divider;
  xSemaphoreGive(s_state_mutex);
  return divider;
}

void tempo_set_time_signature(uint8_t numerator, uint8_t denominator) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_time_signature.numerator = numerator;
  s_time_signature.denominator = denominator;
  // Reset beat counter when time signature changes
  s_beat_counter = 0;
  xSemaphoreGive(s_state_mutex);
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_TIME_SIG_NUM, numerator);
  app_settings_save_u8(NVS_KEY_TIME_SIG_DEN, denominator);
  
  ESP_LOGI(TAG, "Time signature set to %d/%d", numerator, denominator);
}

time_signature_t tempo_get_time_signature(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  time_signature_t sig = s_time_signature;
  xSemaphoreGive(s_state_mutex);
  return sig;
}

void tempo_set_led_sync(bool enabled) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_led_sync_enabled = enabled;
  xSemaphoreGive(s_state_mutex);
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_LED_SYNC, enabled ? 1 : 0);
  
  ESP_LOGI(TAG, "LED sync %s", enabled ? "enabled" : "disabled");
}

bool tempo_get_led_sync(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool enabled = s_led_sync_enabled;
  xSemaphoreGive(s_state_mutex);
  return enabled;
}
