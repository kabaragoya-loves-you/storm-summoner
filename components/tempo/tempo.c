#include "tempo.h"
#include "transport.h"
#include "led.h"
#include "event_bus.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "midi_passthrough.h"
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
#define NVS_KEY_LED_EMPHASIZE "tempo_led_emph"
#define NVS_KEY_LED_RATIO "tempo_led_ratio"
#define NVS_KEY_TIME_SIG_NUM "tempo_ts_num"
#define NVS_KEY_TIME_SIG_DEN "tempo_ts_den"
#define NVS_KEY_CLOCK_STD "tempo_clk_std"
#define NVS_KEY_BPM_DEADZONE "tempo_deadzone"
#define NVS_KEY_CLOCK_OUTPUT "tempo_clk_out"
#define NVS_KEY_CLOCK_ALWAYS "tempo_clk_always"
#define NVS_KEY_CLOCK_NO_PT "tempo_clk_no_pt"

// Constants
#define MIN_BPM 20
#define MAX_BPM 300
#define DEFAULT_BPM 120
#define MIDI_CLOCKS_PER_QUARTER 24

// State variables
static uint16_t s_bpm = DEFAULT_BPM;
static tempo_clock_source_t s_clock_source = CLOCK_SOURCE_INTERNAL;
static tempo_clock_standard_t s_clock_standard = CLOCK_STANDARD_24PPQN;  // Default to 24ppqn
static time_signature_t s_time_signature = {4, 4};  // Default 4/4
static bool s_led_sync_enabled = false;
static bool s_led_emphasize_downbeat = true;  // Make beat 1 different
static uint8_t s_led_flash_ratio = 3;  // Flash duration as % of beat (1-10, default 3%)
static tempo_note_divider_t s_note_divider = DIVIDER_QUARTER;
static uint8_t s_bpm_deadzone = 0;  // BPM change deadzone (0 = no deadzone, 1-5 = ignore ±N BPM changes)
static clock_output_t s_clock_output = CLOCK_OUTPUT_BOTH;  // Where to send clock (USB, UART, BOTH, NONE)
static bool s_clock_always_send = true;  // Send clock even when transport stopped
static bool s_disable_clock_on_passthrough = true;  // Auto-disable clock when passthrough active

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
static void update_midi_out_clock_settings(void);

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
    s_bpm = (uint16_t)(bpm > MAX_BPM ? MAX_BPM : (bpm < MIN_BPM ? MIN_BPM : bpm));
  }
  
  uint8_t led_sync = 0;
  if (app_settings_load_u8(NVS_KEY_LED_SYNC, &led_sync) == ESP_OK) s_led_sync_enabled = (led_sync != 0);
  
  uint8_t led_emphasize = 1;
  if (app_settings_load_u8(NVS_KEY_LED_EMPHASIZE, &led_emphasize) == ESP_OK) s_led_emphasize_downbeat = (led_emphasize != 0);
  
  uint8_t led_ratio = 3;
  if (app_settings_load_u8(NVS_KEY_LED_RATIO, &led_ratio) == ESP_OK && led_ratio >= 1 && led_ratio <= 10) s_led_flash_ratio = led_ratio;
  
  uint8_t ts_num = 4, ts_den = 4;
  app_settings_load_u8(NVS_KEY_TIME_SIG_NUM, &ts_num);
  app_settings_load_u8(NVS_KEY_TIME_SIG_DEN, &ts_den);
  s_time_signature.numerator = ts_num;
  s_time_signature.denominator = ts_den;
  
  // Load clock standard
  uint8_t clk_std = CLOCK_STANDARD_24PPQN;
  if (app_settings_load_u8(NVS_KEY_CLOCK_STD, &clk_std) == APP_SETTINGS_OK) {
    s_clock_standard = (tempo_clock_standard_t)clk_std;
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_STD, (uint8_t)s_clock_standard);
  }
  
  // Note: Clock source is now set by scenes (no NVS persistence here)
  // The scene system calls tempo_set_source() when a scene loads
  
  // Load BPM deadzone
  uint8_t deadzone = 0;
  if (app_settings_load_u8(NVS_KEY_BPM_DEADZONE, &deadzone) == ESP_OK) {
    s_bpm_deadzone = deadzone;
  } else {
    app_settings_save_u8(NVS_KEY_BPM_DEADZONE, s_bpm_deadzone);
  }
  
  // Load clock output settings
  uint8_t clk_out = CLOCK_OUTPUT_BOTH;
  if (app_settings_load_u8(NVS_KEY_CLOCK_OUTPUT, &clk_out) == ESP_OK) {
    s_clock_output = (clock_output_t)clk_out;
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_OUTPUT, (uint8_t)s_clock_output);
  }
  
  uint8_t always_send = 1;
  if (app_settings_load_u8(NVS_KEY_CLOCK_ALWAYS, &always_send) == ESP_OK) {
    s_clock_always_send = (always_send != 0);
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_ALWAYS, s_clock_always_send ? 1 : 0);
  }
  
  uint8_t no_pt = 1;
  if (app_settings_load_u8(NVS_KEY_CLOCK_NO_PT, &no_pt) == ESP_OK) {
    s_disable_clock_on_passthrough = (no_pt != 0);
  } else {
    app_settings_save_u8(NVS_KEY_CLOCK_NO_PT, s_disable_clock_on_passthrough ? 1 : 0);
  }
  
  // Note: update_midi_out_clock_settings() will be called when tempo_start() is called
  // Can't call it here because midi_out_init() hasn't run yet
  
  // Subscribe to transport state changes
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, transport_state_handler, NULL);
  
  const char* std_names[] = {"24ppqn", "16th", "Beat"};
  ESP_LOGI(TAG, "Tempo initialized - BPM: %d, Time Sig: %d/%d, LED Sync: %s, Clock: %s",
    s_bpm, s_time_signature.numerator, s_time_signature.denominator,
    s_led_sync_enabled ? "ON" : "OFF", std_names[s_clock_standard]);
  ESP_LOGI(TAG, "Note: Clock source is now set by scenes");
}

// Helper to update MIDI out tempo settings based on our clock output config
static void update_midi_out_clock_settings(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  clock_output_t output = s_clock_output;
  bool disable_on_passthrough = s_disable_clock_on_passthrough;
  xSemaphoreGive(s_state_mutex);
  
  // Check if we should disable due to passthrough
  if (disable_on_passthrough && (midi_passthrough_usb_to_uart_is_enabled() || midi_passthrough_uart_to_usb_is_enabled())) {
    output = CLOCK_OUTPUT_NONE;
  }
  
  // Configure MIDI out interfaces based on clock_output setting
  bool usb_enabled = (output == CLOCK_OUTPUT_USB || output == CLOCK_OUTPUT_BOTH);
  bool uart_enabled = (output == CLOCK_OUTPUT_UART || output == CLOCK_OUTPUT_BOTH);
  
  midi_out_set_tempo_enabled(MIDI_OUT_INTERFACE_USB, usb_enabled);
  midi_out_set_tempo_enabled(MIDI_OUT_INTERFACE_UART, uart_enabled);
}

static void tempo_task(void *pvParameters) {
  ESP_LOGD(TAG, "Tempo task running");
  
  TickType_t last_wake_time = xTaskGetTickCount();
  uint16_t last_bpm = s_bpm;
  
  while (1) {
    // Check if we should be running based on clock_always_send setting
    bool should_run = s_clock_always_send || transport_is_playing();
    
    if (!should_run) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint16_t current_bpm = s_bpm;
    tempo_clock_source_t source = s_clock_source;
    tempo_clock_standard_t standard = s_clock_standard;
    xSemaphoreGive(s_state_mutex);
    
    if (source == CLOCK_SOURCE_INTERNAL) {
      // Calculate pulses per quarter note based on clock standard
      uint32_t ppqn;
      switch (standard) {
        case CLOCK_STANDARD_24PPQN:
          ppqn = 24;  // Standard MIDI clock
          break;
        case CLOCK_STANDARD_16TH_NOTE:
          ppqn = 6;   // 1 pulse per 16th note (1/4 of 24ppqn)
          break;
        case CLOCK_STANDARD_BEAT:
          ppqn = 1;   // 1 pulse per beat (1/24 of 24ppqn)
          break;
        default:
          ppqn = 24;
          break;
      }
      
      // Calculate tick interval (minimum 10ms to ensure at least 1 FreeRTOS tick)
      uint32_t tick_interval_ms = 60000 / (ppqn * current_bpm);
      if (tick_interval_ms < 10) tick_interval_ms = 10;
      
      // Send MIDI clock directly (low latency requirement)
      send_clock();
      
      // Track ticks and beats (use full 24ppqn for beat tracking)
      s_tick_counter++;
      
      // Check if we've completed a beat (based on divider)
      // Note: s_note_divider is based on 24ppqn, so we need to scale it
      uint32_t beat_divisor = s_note_divider;
      if (standard == CLOCK_STANDARD_16TH_NOTE) {
        beat_divisor /= 4;  // Adjust for 6ppqn
      } else if (standard == CLOCK_STANDARD_BEAT) {
        beat_divisor = 1;   // One tick = one beat
      }
      
      if (beat_divisor > 0 && (s_tick_counter % beat_divisor == 0)) {
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
            uint16_t new_bpm = (uint16_t)(60000 / interval_ms);
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
      // In MIDI clock mode, we track incoming clocks (in tempo_midi_clock_tick)
      // but still send outgoing clocks based on the tracked BPM
      
      // Calculate pulses per quarter note based on clock standard
      uint32_t ppqn;
      switch (standard) {
        case CLOCK_STANDARD_24PPQN:
          ppqn = 24;  // Standard MIDI clock
          break;
        case CLOCK_STANDARD_16TH_NOTE:
          ppqn = 6;   // 1 pulse per 16th note
          break;
        case CLOCK_STANDARD_BEAT:
          ppqn = 1;   // 1 pulse per beat
          break;
        default:
          ppqn = 24;
          break;
      }
      
      // Calculate tick interval based on tracked BPM (minimum 10ms to ensure at least 1 FreeRTOS tick)
      uint32_t tick_interval_ms = 60000 / (ppqn * current_bpm);
      if (tick_interval_ms < 10) tick_interval_ms = 10;
      
      // Send MIDI clock
      send_clock();
      
      // Track ticks and beats
      s_tick_counter++;
      
      // Check if we've completed a beat
      uint32_t beat_divisor = s_note_divider;
      if (standard == CLOCK_STANDARD_16TH_NOTE) {
        beat_divisor /= 4;
      } else if (standard == CLOCK_STANDARD_BEAT) {
        beat_divisor = 1;
      }
      
      if (beat_divisor > 0 && (s_tick_counter % beat_divisor == 0)) {
        s_beat_counter++;
        if (s_beat_counter > s_time_signature.numerator) s_beat_counter = 1;
        publish_beat_event();
      }
      
      // Delay until next tick
      vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(tick_interval_ms));
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
      // Don't stop tempo task - it respects clock_always_send setting
      // Just reset counters
      s_beat_counter = 0;
      s_tick_counter = 0;
      break;
      
    case TRANSPORT_PAUSED:
      // Don't stop tempo task
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
    
    // Calculate flash duration based on ratio (percentage of beat)
    duration_ms = (beat_duration_ms * s_led_flash_ratio) / 100;
    
    // Emphasize downbeat if enabled (2x longer)
    if (s_led_emphasize_downbeat && s_beat_counter == 1) {
      duration_ms *= 2;
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
    // Update MIDI out settings before starting task
    update_midi_out_clock_settings();
    
    BaseType_t ret = xTaskCreate(tempo_task, "tempo", 3072, NULL, TASK_PRIORITY_MIDI_TEMPO, &s_tempo_task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create tempo task");
    } else {
      // Publish initial tempo
      publish_tempo_changed_event();
    }
  }
}

void tempo_stop(void) {
  if (s_tempo_task_handle != NULL) {
    vTaskDelete(s_tempo_task_handle);
    s_tempo_task_handle = NULL;
    ESP_LOGD(TAG, "Tempo task stopped");
  }
}

void tempo_set_bpm(uint16_t bpm) {
  if (bpm < MIN_BPM) bpm = MIN_BPM;
  if (bpm > MAX_BPM) bpm = MAX_BPM;
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_bpm != bpm) {
    s_bpm = bpm;
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
  
  // Note: No NVS save - clock source is now a per-scene setting
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGD(TAG, "Clock source set to %s", source_str);
}

tempo_clock_source_t tempo_get_source(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_clock_source_t source = s_clock_source;
  xSemaphoreGive(s_state_mutex);
  return source;
}

void tempo_sync_pulse(void) {
  // Note: Now always processes if source is SYNC (set by scene)
  // Scene controls clock source, so no extra gating needed
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
  static uint32_t midi_tick_count = 0;
  static uint32_t last_quarter_time = 0;
  static float ema_interval_ms = 0.0f;  // Exponential moving average of quarter note intervals
  static bool ema_initialized = false;
  static uint32_t last_update_time = 0;  // Rate limit BPM updates
  
  // Safety: Don't process if tempo not initialized yet
  if (!s_state_mutex) return;
  
  // Only process if source is MIDI
  if (s_clock_source != CLOCK_SOURCE_MIDI) return;
  
  midi_tick_count++;
  
  uint32_t now = esp_timer_get_time() / 1000;
  
  // EMA-based tempo tracking with outlier rejection
  // Updates smoothly every quarter note, but rate-limits BPM announcements
  
  if (midi_tick_count % MIDI_CLOCKS_PER_QUARTER == 0) {
    // Every quarter note: measure interval and update EMA
    
    if (last_quarter_time > 0) {
      uint32_t interval_ms = now - last_quarter_time;
      
      // Sanity check: 200ms (300 BPM) to 3000ms (20 BPM)
      if (interval_ms >= 200 && interval_ms <= 3000) {
        
        if (!ema_initialized) {
          // First measurement - initialize EMA
          ema_interval_ms = (float)interval_ms;
          ema_initialized = true;
        } else {
          // Adaptive outlier rejection:
          // Small changes (±20%): Apply EMA smoothing
          // Large changes (>20%): Reset EMA to adapt quickly to tempo changes
          float deviation = (float)interval_ms / ema_interval_ms;
          
          if (deviation >= 0.80f && deviation <= 1.20f) {
            // Within ±20% - valid measurement, update EMA with smoothing
            // Adaptive alpha: higher at fast tempos for better jitter filtering
            // Fast tempos (<300ms): alpha=0.5 (more smoothing needed)
            // Normal tempos (300-1000ms): alpha=0.4 
            // Slow tempos (>1000ms): alpha=0.3 (less data, need more history)
            float alpha = (ema_interval_ms < 300.0f) ? 0.5f : 
                         (ema_interval_ms > 1000.0f) ? 0.3f : 0.4f;
            ema_interval_ms = alpha * (float)interval_ms + (1.0f - alpha) * ema_interval_ms;
          } else {
            // Beyond ±20% - likely tempo change
            // Reset EMA immediately to adapt to new tempo
            ema_interval_ms = (float)interval_ms;
            ESP_LOGD(TAG, "Large tempo change: %.0f ms -> %lu ms", 
                     ema_interval_ms, (unsigned long)interval_ms);
          }
        }
        
        // Calculate BPM from EMA interval
        uint16_t calculated_bpm = (uint16_t)(60000.0f / ema_interval_ms);
        
        // Clamp to valid range (20-300 BPM)
        if (calculated_bpm < MIN_BPM) calculated_bpm = MIN_BPM;
        if (calculated_bpm > MAX_BPM) calculated_bpm = MAX_BPM;
        
        uint16_t measured_bpm = calculated_bpm;
        
        if (measured_bpm >= MIN_BPM && measured_bpm <= MAX_BPM) {
          int bpm_delta = abs((int)measured_bpm - (int)s_bpm);
          
          // Apply deadzone if configured
          if (bpm_delta > s_bpm_deadzone) {
            // Rate limit updates: minimum 500ms between BPM announcements
            // This prevents spam at fast tempos while still tracking smoothly
            if (last_update_time == 0 || (now - last_update_time) >= 500) {
              xSemaphoreTake(s_state_mutex, portMAX_DELAY);
              s_bpm = measured_bpm;
              xSemaphoreGive(s_state_mutex);
              publish_tempo_changed_event();
              last_update_time = now;
            }
          }
        }
      }
    }
    
    last_quarter_time = now;
  }
  
  // Check for beat (for beat events and LED sync)
  if (midi_tick_count % s_note_divider == 0) {
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
  
  // If enabling LED sync, stop flicker (will restore when disabled)
  if (enabled) {
    if (flicker_is_running()) {
      flicker_stop();
      ESP_LOGI(TAG, "Stopped flicker for tempo sync");
    }
  } else {
    // If disabling and user had flicker preference enabled, restart it
    if (led_get_flicker_preference()) {
      flicker_start();
      ESP_LOGI(TAG, "Restored flicker based on user preference");
    }
  }
  
  ESP_LOGI(TAG, "LED sync %s", enabled ? "enabled" : "disabled");
}

bool tempo_get_led_sync(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool enabled = s_led_sync_enabled;
  xSemaphoreGive(s_state_mutex);
  return enabled;
}

void tempo_set_led_emphasize_downbeat(bool emphasize) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_led_emphasize_downbeat = emphasize;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_LED_EMPHASIZE, emphasize ? 1 : 0);
  ESP_LOGI(TAG, "LED downbeat emphasis %s", emphasize ? "enabled" : "disabled");
}

bool tempo_get_led_emphasize_downbeat(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool emphasize = s_led_emphasize_downbeat;
  xSemaphoreGive(s_state_mutex);
  return emphasize;
}

void tempo_set_led_flash_ratio(uint8_t ratio) {
  if (ratio < 1 || ratio > 10) {
    ESP_LOGW(TAG, "LED flash ratio must be 1-10%%");
    return;
  }
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_led_flash_ratio = ratio;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_LED_RATIO, ratio);
  ESP_LOGI(TAG, "LED flash ratio set to %d%% of beat duration", ratio);
}

uint8_t tempo_get_led_flash_ratio(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t ratio = s_led_flash_ratio;
  xSemaphoreGive(s_state_mutex);
  return ratio;
}

void tempo_set_clock_standard(tempo_clock_standard_t standard) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_standard = standard;
  xSemaphoreGive(s_state_mutex);
  
  // Save to NVS
  app_settings_save_u8(NVS_KEY_CLOCK_STD, (uint8_t)standard);
  
  const char* std_names[] = {"24ppqn (DIN Sync)", "16th note (Korg Volca)", "Beat (Modular)"};
  ESP_LOGI(TAG, "Clock standard set to %s", std_names[standard]);
}

tempo_clock_standard_t tempo_get_clock_standard(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  tempo_clock_standard_t standard = s_clock_standard;
  xSemaphoreGive(s_state_mutex);
  return standard;
}

void tempo_set_bpm_deadzone(uint8_t deadzone) {
  if (deadzone > 5) {
    ESP_LOGW(TAG, "BPM deadzone clamped to max of 5");
    deadzone = 5;
  }
  
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_bpm_deadzone = deadzone;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_BPM_DEADZONE, deadzone);
  ESP_LOGI(TAG, "BPM deadzone set to %u", (unsigned)deadzone);
}

uint8_t tempo_get_bpm_deadzone(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  uint8_t deadzone = s_bpm_deadzone;
  xSemaphoreGive(s_state_mutex);
  return deadzone;
}

void tempo_set_clock_output(clock_output_t output) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_output = output;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_OUTPUT, (uint8_t)output);
  
  const char* output_names[] = {"None", "USB", "UART", "Both"};
  ESP_LOGI(TAG, "Clock output set to: %s", output_names[output]);
  
  // Update MIDI out settings immediately
  update_midi_out_clock_settings();
}

clock_output_t tempo_get_clock_output(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  clock_output_t output = s_clock_output;
  xSemaphoreGive(s_state_mutex);
  return output;
}

void tempo_set_clock_always_send(bool always_send) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_clock_always_send = always_send;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_ALWAYS, always_send ? 1 : 0);
  ESP_LOGI(TAG, "Clock always send: %s", always_send ? "enabled" : "disabled");
}

bool tempo_get_clock_always_send(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool always_send = s_clock_always_send;
  xSemaphoreGive(s_state_mutex);
  return always_send;
}

void tempo_set_disable_clock_on_passthrough(bool disable) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  s_disable_clock_on_passthrough = disable;
  xSemaphoreGive(s_state_mutex);
  
  app_settings_save_u8(NVS_KEY_CLOCK_NO_PT, disable ? 1 : 0);
  ESP_LOGI(TAG, "Disable clock on passthrough: %s", disable ? "enabled" : "disabled");
  
  // Update MIDI out settings immediately
  update_midi_out_clock_settings();
}

bool tempo_get_disable_clock_on_passthrough(void) {
  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool disable = s_disable_clock_on_passthrough;
  xSemaphoreGive(s_state_mutex);
  return disable;
}
