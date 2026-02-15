#include "clock_sync.h"
#include "event_bus.h"
#include "app_settings.h"
#include "tempo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "task_priorities.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "io.h"

#define TAG "CLOCK_SYNC"

// NVS keys
#define NVS_KEY_SYNC_MODE "sync_mode"

// Constants
#define SYNC_TIMEOUT_MS 2000       // Consider sync lost after 2 seconds
#define MIN_BPM 30
#define MAX_BPM 250
#define PULSE_BUFFER_SIZE 8        // Average over last 8 pulse intervals
#define DEBOUNCE_TIME_MS 50        // 50ms debounce for edge detection (rejects glitches)

// State
static clock_sync_mode_t s_mode = CLOCK_SYNC_24PPQN;
static bool s_sync_active = false;
static uint8_t s_current_bpm = 0;
static bool s_enabled = false;

// Pulse detection
static uint32_t s_last_pulse_time = 0;
static uint32_t s_pulse_intervals[PULSE_BUFFER_SIZE] = {0};
static uint8_t s_pulse_index = 0;
static uint8_t s_pulse_count = 0;
static uint32_t s_pulse_counter = 0;  // Count pulses for PPQN divider

// ISR queue for edge detection
static QueueHandle_t s_gpio_evt_queue = NULL;

// Forward declarations
static void clock_sync_task(void *pvParameters);
static uint8_t calculate_bpm_from_interval(uint32_t interval_ms, clock_sync_mode_t mode);

// ISR handler for clock pulses
static void IRAM_ATTR clock_sync_isr(void *arg) {
  uint32_t now = esp_timer_get_time() / 1000; // Convert to ms
  xQueueSendFromISR(s_gpio_evt_queue, &now, NULL);
}

esp_err_t clock_sync_init(void) {
  ESP_LOGD(TAG, "Initializing clock sync component");
  
  // Load settings from NVS
  uint8_t mode = CLOCK_SYNC_24PPQN;
  app_settings_load_u8(NVS_KEY_SYNC_MODE, &mode);
  s_mode = (clock_sync_mode_t)mode;

  // Note: GPIO configuration for clock sync will be done in clock_sync_enable()
  // when we switch from ADC mode to GPIO mode. PIN_CV_CLOCK is shared with CV input.
  // Voltage range is controlled by the CV component (same hardware).
  
  // Create a queue to handle gpio events from isr
  s_gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
  
  ESP_LOGI(TAG, "Clock sync initialized - Mode: %d", s_mode);
  
  return ESP_OK;
}

void clock_sync_enable(void) {
  if (!s_enabled) {
    s_enabled = true;
    
    // Configure the shared CV/clock pin as GPIO input
    // (When CV mode is active, this pin is configured as ADC)
    gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_CV_CLOCK),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE  // Set after adding ISR handler
    };
    gpio_config(&io_conf);
    
    // Create clock sync task
    xTaskCreate(clock_sync_task, "clock_sync", 2048, NULL, TASK_PRIORITY_SYNC_BPM, NULL);
    
    // Add ISR handler
    gpio_isr_handler_add(PIN_CV_CLOCK, clock_sync_isr, NULL);
    
    // Enable interrupt on rising edge (inverted signal: high = pulse)
    gpio_set_intr_type(PIN_CV_CLOCK, GPIO_INTR_POSEDGE);
    
    ESP_LOGI(TAG, "Clock sync enabled on GPIO%d (shared with CV input)", PIN_CV_CLOCK);
  }
}

void clock_sync_disable(void) {
  if (s_enabled) {
    s_enabled = false;
    
    // Disable interrupt
    gpio_set_intr_type(PIN_CV_CLOCK, GPIO_INTR_DISABLE);
    gpio_isr_handler_remove(PIN_CV_CLOCK);
    
    // Reset the GPIO pin (it will be reconfigured as ADC when CV mode is enabled)
    gpio_reset_pin(PIN_CV_CLOCK);
    
    s_sync_active = false;
    s_current_bpm = 0;
    s_pulse_count = 0;
    
    ESP_LOGI(TAG, "Clock sync disabled");
  }
}

static void clock_sync_task(void *pvParameters) {
  uint32_t pulse_time;
  uint32_t last_pulse_time = 0;
  
  while (s_enabled) {
    if (xQueueReceive(s_gpio_evt_queue, &pulse_time, pdMS_TO_TICKS(100))) {
      // Debounce - ignore pulses too close together
      if (last_pulse_time > 0 && (pulse_time - last_pulse_time) < DEBOUNCE_TIME_MS) continue;
      
      s_pulse_counter++;
      
      // Handle different clock modes
      if (s_mode == CLOCK_SYNC_HALF_BEAT) {
        // Special mode: 1 pulse = half beat, so send 2 tempo pulses per received pulse
        // Calculate half the interval for spacing
        uint32_t half_interval = 0;
        if (s_last_pulse_time > 0 && pulse_time > s_last_pulse_time) {
          half_interval = (pulse_time - s_last_pulse_time) / 2;
        }
        
        // Send first pulse immediately
        tempo_sync_pulse();
        
        // Send second pulse after half the interval (if we have timing data)
        if (half_interval > 0 && half_interval < 2000) {
          vTaskDelay(pdMS_TO_TICKS(half_interval));
          tempo_sync_pulse();
        }
      } else {
        // Standard PPQN modes: divide incoming pulses
        uint32_t ppqn = 1;
        switch (s_mode) {
          case CLOCK_SYNC_24PPQN: ppqn = 24; break;
          case CLOCK_SYNC_48PPQN: ppqn = 48; break;
          case CLOCK_SYNC_96PPQN: ppqn = 96; break;
          case CLOCK_SYNC_1PPQ: ppqn = 1; break;
          case CLOCK_SYNC_2PPQ: ppqn = 2; break;
          case CLOCK_SYNC_4PPQ: ppqn = 4; break;
          default: ppqn = 1; break;
        }
        
        // Only notify tempo on quarter note boundaries
        if (s_pulse_counter % ppqn == 0) {
          tempo_sync_pulse();
        }
      }
      
      // Post sync pulse event for other components (every pulse)
      event_t sync_event = {
        .type = EVENT_CLOCK_SYNC_PULSE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp()
      };
      event_bus_post(&sync_event);
      
      // Calculate interval since last pulse
      if (s_last_pulse_time > 0) {
        uint32_t interval = pulse_time - s_last_pulse_time;
        
        // Store interval in circular buffer
        s_pulse_intervals[s_pulse_index] = interval;
        s_pulse_index = (s_pulse_index + 1) % PULSE_BUFFER_SIZE;
        if (s_pulse_count < PULSE_BUFFER_SIZE) s_pulse_count++;
        
        // Calculate average interval
        if (s_pulse_count >= 2) {
          uint32_t total_interval = 0;
          for (int i = 0; i < s_pulse_count; i++) total_interval += s_pulse_intervals[i];
          uint32_t avg_interval = total_interval / s_pulse_count;
          
          // Calculate BPM
          uint8_t new_bpm = calculate_bpm_from_interval(avg_interval, s_mode);
          
          if (new_bpm >= MIN_BPM && new_bpm <= MAX_BPM) {
            if (new_bpm != s_current_bpm) {
              s_current_bpm = new_bpm;
              ESP_LOGI(TAG, "Sync BPM: %d (interval: %lums)", s_current_bpm, avg_interval);
            }
            s_sync_active = true;
          }
        }
      }
      
      s_last_pulse_time = pulse_time;
      last_pulse_time = pulse_time;
    } else {
      // Check for sync timeout
      if (s_sync_active && s_last_pulse_time > 0) {
        uint32_t now = esp_timer_get_time() / 1000;
        if ((now - s_last_pulse_time) > SYNC_TIMEOUT_MS) {
          s_sync_active = false;
          s_current_bpm = 0;
          s_pulse_count = 0;
          ESP_LOGI(TAG, "Clock sync lost");
        }
      }
    }
  }
  
  vTaskDelete(NULL);
}

static uint8_t calculate_bpm_from_interval(uint32_t interval_ms, clock_sync_mode_t mode) {
  if (interval_ms == 0) return 0;
  
  uint32_t ms_per_beat;
  
  switch (mode) {
    case CLOCK_SYNC_24PPQN:
      ms_per_beat = interval_ms * 24;
      break;
    case CLOCK_SYNC_48PPQN:
      ms_per_beat = interval_ms * 48;
      break;
    case CLOCK_SYNC_96PPQN:
      ms_per_beat = interval_ms * 96;
      break;
    case CLOCK_SYNC_1PPQ:
      ms_per_beat = interval_ms;
      break;
    case CLOCK_SYNC_2PPQ:
      ms_per_beat = interval_ms * 2;
      break;
    case CLOCK_SYNC_4PPQ:
      ms_per_beat = interval_ms * 4;
      break;
    default:
      ms_per_beat = interval_ms * 24;
      break;
  }
  
  // BPM = 60000 / ms_per_beat
  return (uint8_t)(60000 / ms_per_beat);
}

// Public API functions
void clock_sync_set_mode(clock_sync_mode_t mode) {
  s_mode = mode;
  s_pulse_count = 0; // Reset averaging on mode change
  s_pulse_counter = 0; // Reset PPQN counter
  app_settings_save_u8(NVS_KEY_SYNC_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "Clock sync mode set to %d", mode);
}

clock_sync_mode_t clock_sync_get_mode(void) {
  return s_mode;
}

uint8_t clock_sync_get_bpm(void) {
  return s_sync_active ? s_current_bpm : 0;
}

bool clock_sync_is_active(void) {
  return s_sync_active;
}