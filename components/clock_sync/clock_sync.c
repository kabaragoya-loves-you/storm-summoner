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
#include <string.h>

// Analog clock sync shares the CV jack (PIN_CV_CLOCK). The front-end has no
// AC-coupling capacitor, so line-level audio clicks may carry DC offset and
// ringing that produce extra GPIO edges. Software debounce rejects glitches;
// a hardware comparator may be needed for reliable 24 PPQ at high BPM.
//
// CLOCK_SYNC_HALF_BEAT uses vTaskDelay inside clock_sync_task while injecting
// a second tempo pulse. That blocks the task from draining the edge queue and
// is a known concurrency limitation (not required for 24 PPQ audio-sync).

#define TAG "CLOCK_SYNC"

// NVS keys
#define NVS_KEY_SYNC_MODE "sync_mode"

// Constants
#define SYNC_TIMEOUT_MS 5000       // Consider sync lost after 5 seconds idle
#define PULSE_BUFFER_SIZE 8        // Average over last 8 pulse intervals
// 1 PPQ @ 20 BPM = 3000 ms; allow headroom for jitter below MIN tempo.
#define MAX_INTERVAL_FOR_BPM_MS 3600
// Shortest valid quarter interval: 300 BPM + jitter margin for tracking into clamp.
#define MIN_INTERVAL_FOR_BPM_MS 190
#define SPEEDUP_RESET_PCT 5
#define SPEEDUP_TRACK_PCT 2
#define DEBOUNCE_MIN_MS 2
#define DEBOUNCE_MAX_MS 50
#define DEBOUNCE_GUARD_NUM 45
#define DEBOUNCE_GUARD_DEN 100

// State
static clock_sync_mode_t s_mode = CLOCK_SYNC_24PPQN;
static bool s_sync_active = false;
static uint16_t s_current_bpm_x10 = 0;
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
static uint16_t calculate_bpm_x10_from_interval(uint32_t interval_ms, clock_sync_mode_t mode);
static uint32_t clock_sync_ppq_for_mode(clock_sync_mode_t mode);
static uint32_t clock_sync_debounce_ms(void);

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

static uint32_t clock_sync_ppq_for_mode(clock_sync_mode_t mode) {
  switch (mode) {
    case CLOCK_SYNC_24PPQN: return 24;
    case CLOCK_SYNC_48PPQN: return 48;
    case CLOCK_SYNC_96PPQN: return 96;
    case CLOCK_SYNC_2PPQ: return 2;
    case CLOCK_SYNC_4PPQ: return 4;
    case CLOCK_SYNC_HALF_BEAT: return 2;
    default: return 1;
  }
}

static uint32_t clock_sync_debounce_ms(void) {
  uint32_t ppq = clock_sync_ppq_for_mode(s_mode);
  if (ppq == 0) ppq = 1;
  uint32_t min_interval = 60000 / (300 * ppq);
  if (min_interval < DEBOUNCE_MIN_MS) min_interval = DEBOUNCE_MIN_MS;
  uint32_t debounce = (min_interval * DEBOUNCE_GUARD_NUM) / DEBOUNCE_GUARD_DEN;
  if (debounce < DEBOUNCE_MIN_MS) debounce = DEBOUNCE_MIN_MS;
  if (debounce > DEBOUNCE_MAX_MS) debounce = DEBOUNCE_MAX_MS;
  return debounce;
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
    // 4096: ESP_LOGI via USB plus tempo_set_bpm/event_bus need headroom at fast tempos.
    xTaskCreate(clock_sync_task, "clock_sync", 4096, NULL, TASK_PRIORITY_SYNC_BPM, NULL);
    
    // Add ISR handler
    gpio_isr_handler_add(PIN_CV_CLOCK, clock_sync_isr, NULL);
    
    // Enable interrupt on rising edge (inverted signal: high = pulse)
    gpio_set_intr_type(PIN_CV_CLOCK, GPIO_INTR_POSEDGE);

    tempo_set_analog_sync_bpm_active(true);
    ESP_LOGI(TAG, "Clock sync enabled on GPIO%d (shared with CV input)", PIN_CV_CLOCK);
  }
}

void clock_sync_disable(void) {
  if (s_enabled) {
    s_enabled = false;
    tempo_set_analog_sync_bpm_active(false);
    
    // Disable interrupt
    gpio_set_intr_type(PIN_CV_CLOCK, GPIO_INTR_DISABLE);
    gpio_isr_handler_remove(PIN_CV_CLOCK);
    
    // Reset the GPIO pin (it will be reconfigured as ADC when CV mode is enabled)
    gpio_reset_pin(PIN_CV_CLOCK);
    
    s_sync_active = false;
    s_current_bpm_x10 = 0;
    s_pulse_count = 0;
    
    ESP_LOGI(TAG, "Clock sync disabled");
  }
}

static uint32_t clock_sync_median_interval(uint32_t *intervals, uint8_t count) {
  if (count == 0) return 0;
  static uint32_t scratch[PULSE_BUFFER_SIZE];
  memcpy(scratch, intervals, count * sizeof(uint32_t));
  for (uint8_t i = 1; i < count; i++) {
    uint32_t key = scratch[i];
    int j = (int)i - 1;
    while (j >= 0 && scratch[j] > key) {
      scratch[j + 1] = scratch[j];
      j--;
    }
    scratch[j + 1] = key;
  }
  return scratch[count / 2];
}

static uint32_t clock_sync_interval_for_bpm(uint32_t latest_iv) {
  static uint32_t valid[PULSE_BUFFER_SIZE];
  uint8_t n = 0;
  for (uint8_t i = 0; i < s_pulse_count; i++) {
    uint32_t iv = s_pulse_intervals[i];
    if (iv < MIN_INTERVAL_FOR_BPM_MS || iv > MAX_INTERVAL_FOR_BPM_MS) continue;
    if (iv < clock_sync_debounce_ms()) continue;
    valid[n++] = iv;
  }
  if (n < 2) return 0;

  uint32_t med = clock_sync_median_interval(valid, n);
  if (latest_iv >= MIN_INTERVAL_FOR_BPM_MS && latest_iv <= MAX_INTERVAL_FOR_BPM_MS &&
      latest_iv < med && (med - latest_iv) * 100 > med * SPEEDUP_TRACK_PCT) {
    uint32_t min_iv = valid[0];
    for (uint8_t i = 1; i < n; i++)
      if (valid[i] < min_iv) min_iv = valid[i];
    return min_iv;
  }
  return med;
}

static void clock_sync_task(void *pvParameters) {
  uint32_t pulse_time;
  uint32_t last_pulse_time = 0;

  while (s_enabled) {
    if (xQueueReceive(s_gpio_evt_queue, &pulse_time, pdMS_TO_TICKS(100))) {
      uint32_t debounce_ms = clock_sync_debounce_ms();
      if (last_pulse_time > 0 && (pulse_time - last_pulse_time) < debounce_ms) {
        ESP_LOGD(TAG, "edge rejected: interval %lums < debounce %lums",
          (unsigned long)(pulse_time - last_pulse_time), (unsigned long)debounce_ms);
        continue;
      }

      uint32_t interval_ms = 0;
      if (last_pulse_time > 0 && pulse_time > last_pulse_time)
        interval_ms = pulse_time - last_pulse_time;

      s_pulse_counter++;

      // Handle different clock modes
      if (s_mode == CLOCK_SYNC_HALF_BEAT) {
        uint32_t half_interval = 0;
        if (s_last_pulse_time > 0 && pulse_time > s_last_pulse_time)
          half_interval = (pulse_time - s_last_pulse_time) / 2;

        tempo_sync_pulse();

        if (half_interval > 0 && half_interval < 2000) {
          vTaskDelay(pdMS_TO_TICKS(half_interval));
          tempo_sync_pulse();
        }
      } else {
        uint32_t ppqn = clock_sync_ppq_for_mode(s_mode);
        if (s_pulse_counter % ppqn == 0)
          tempo_sync_pulse();
      }

      event_t sync_event = {
        .type = EVENT_CLOCK_SYNC_PULSE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp()
      };
      event_bus_post(&sync_event);

      if (s_last_pulse_time > 0) {
        interval_ms = pulse_time - s_last_pulse_time;

        // Drop stale intervals when tempo jumps so median does not stay locked.
        if (interval_ms > 0 && s_pulse_count >= 1) {
          uint8_t prev_idx = (uint8_t)((s_pulse_index + PULSE_BUFFER_SIZE - 1) %
            PULSE_BUFFER_SIZE);
          uint32_t prev_iv = s_pulse_intervals[prev_idx];
          if (prev_iv > 0) {
            uint32_t diff = (interval_ms > prev_iv) ? interval_ms - prev_iv : prev_iv - interval_ms;
            bool big_jump = diff * 4 > prev_iv;
            bool speeding_up = interval_ms < prev_iv &&
              (prev_iv - interval_ms) * 100 > prev_iv * SPEEDUP_RESET_PCT;
            if (big_jump || speeding_up) {
              s_pulse_count = 0;
              s_pulse_index = 0;
              s_current_bpm_x10 = 0;
            }
          }
        }

        s_pulse_intervals[s_pulse_index] = interval_ms;
        s_pulse_index = (s_pulse_index + 1) % PULSE_BUFFER_SIZE;
        if (s_pulse_count < PULSE_BUFFER_SIZE) s_pulse_count++;

        if (s_pulse_count >= 2) {
          uint32_t avg_interval = clock_sync_interval_for_bpm(interval_ms);
          if (avg_interval == 0) goto pulse_done;

          uint16_t new_bpm_x10 = calculate_bpm_x10_from_interval(avg_interval, s_mode);

          if (new_bpm_x10 < TEMPO_MIN_BPM_X10)
            new_bpm_x10 = TEMPO_MIN_BPM_X10;
          else if (new_bpm_x10 > TEMPO_MAX_BPM_X10)
            new_bpm_x10 = TEMPO_MAX_BPM_X10;

          if (new_bpm_x10 != s_current_bpm_x10) {
            s_current_bpm_x10 = new_bpm_x10;
            if (tempo_get_source() == CLOCK_SOURCE_SYNC)
              tempo_set_bpm_x10(new_bpm_x10);
          }
          s_sync_active = true;
        }
      pulse_done:
      }

      s_last_pulse_time = pulse_time;
      last_pulse_time = pulse_time;
    } else {
      // Check for sync timeout
      if (s_sync_active && s_last_pulse_time > 0) {
        uint32_t now = esp_timer_get_time() / 1000;
        if ((now - s_last_pulse_time) > SYNC_TIMEOUT_MS) {
          s_sync_active = false;
          s_current_bpm_x10 = 0;
          ESP_LOGI(TAG, "Clock sync lost");
        }
      }
    }
  }
  
  vTaskDelete(NULL);
}

static uint16_t calculate_bpm_x10_from_interval(uint32_t interval_ms, clock_sync_mode_t mode) {
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
    case CLOCK_SYNC_HALF_BEAT:
      ms_per_beat = interval_ms * 2;
      break;
    default:
      ms_per_beat = interval_ms * 24;
      break;
  }
  
  uint32_t bpm_x10 = (600000U + ms_per_beat / 2) / ms_per_beat;
  if (!tempo_get_allow_fractional_bpm()) {
    uint16_t whole = tempo_x10_to_whole((uint16_t)bpm_x10);
    bpm_x10 = tempo_whole_to_x10(whole);
  }
  return (uint16_t)bpm_x10;
}

// Public API functions
void clock_sync_set_mode(clock_sync_mode_t mode) {
  s_mode = mode;
  s_pulse_count = 0;
  s_pulse_counter = 0;
  app_settings_save_u8(NVS_KEY_SYNC_MODE, (uint8_t)mode);
  ESP_LOGI(TAG, "Clock sync mode set to %d (debounce %lums)", mode,
    (unsigned long)clock_sync_debounce_ms());
}

clock_sync_mode_t clock_sync_get_mode(void) {
  return s_mode;
}

uint8_t clock_sync_get_bpm(void) {
  return s_sync_active ? (uint8_t)tempo_x10_to_whole(s_current_bpm_x10) : 0;
}

bool clock_sync_is_active(void) {
  return s_sync_active;
}