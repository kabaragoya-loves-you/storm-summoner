#include "midi_tempo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "midi_messages.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <inttypes.h>
#include "cv.h"
#include "analog_input.h"
#include "task_priorities.h"
#include "event_bus.h"

void midi_tempo_event_handler_init(void);

#define TAG "MIDI_TEMPO"
#define LED_DEFAULT_ON_PERCENT 15  // 15% of quarter note duration
#define PIN_LED 15

#define NVS_KEY_BPM "midi_tempo_bpm"

static uint16_t global_bpm = 120;
static midi_clock_source_t clock_source = CLOCK_SOURCE_INTERNAL;

static TaskHandle_t tempo_send_task_handle = NULL;
static TaskHandle_t sync_bpm_task_handle = NULL;

static SemaphoreHandle_t sync_semaphore = NULL;

static uint32_t last_sync_tick_ms = 0;

static bool quarter_note_log_enabled = false;
static uint8_t note_divider = DIVIDER_QUARTER; // default: quarter note = 24 ticks
static uint32_t tick_counter = 0;

#define TAP_BUFFER_SIZE 4
#define TAP_TIMEOUT_MS 2000
static uint32_t tap_timestamps[TAP_BUFFER_SIZE] = {0};
static int tap_count = 0;
static int tap_index = 0;

void blink_led_on_quarter_note(void);

//------------------------------------------------------------
// sync_bpm_task: Only used when clock_source == CLOCK_SOURCE_SYNC.
// Waits on sync pulses and updates global_bpm.
//------------------------------------------------------------
static void sync_bpm_task(void *pvParameters) {
  while (1) {
    if (xSemaphoreTake(sync_semaphore, portMAX_DELAY) == pdTRUE) {
      uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (last_sync_tick_ms != 0) {
        uint32_t interval = now_ms - last_sync_tick_ms;
        if (interval > 0) {
          // Each sync pulse represents an eighth note; double the BPM.
          uint16_t new_bpm = (60000 * 2 + interval / 2) / interval;
          global_bpm = new_bpm;
          ESP_LOGI(TAG, "Sync BPM updated to %d", global_bpm);
        }
      }
      last_sync_tick_ms = now_ms;
    }
  }
}

//------------------------------------------------------------
// midi_tempo_send_task: Runs when clock_source == INTERNAL or SYNC.
// Sends MIDI clock pulses at a rate derived from global_bpm.
//------------------------------------------------------------
static void midi_tempo_send_task(void *pvParameters) {
  while (1) {
    // Calculate the interval (in ms) for one MIDI clock tick.
    // 24 ticks per quarter note.
    uint32_t tick_interval_ms = 60000 / (24 * global_bpm);
    send_clock();
    tick_counter++;
    if (quarter_note_log_enabled && (tick_counter % note_divider == 0)) {
      blink_led_on_quarter_note();
      ESP_LOGI(TAG, "Note divider tick: BPM %d", global_bpm);
    }
    vTaskDelay(pdMS_TO_TICKS(tick_interval_ms));
  }
}

void midi_tempo_init(void) {
  sync_semaphore = xSemaphoreCreateBinary();
  if (sync_semaphore == NULL) ESP_LOGE(TAG, "Failed to create sync semaphore");

  uint16_t saved_bpm;
  if (app_settings_load_u16(NVS_KEY_BPM, &saved_bpm) == ESP_OK) {
    global_bpm = saved_bpm;
    ESP_LOGI(TAG, "Loaded saved BPM: %d", global_bpm);
  } else {
    ESP_LOGI(TAG, "Using default BPM: %d", global_bpm);
  }

  ESP_LOGI(TAG, "MIDI Tempo module initialized");
  
  midi_tempo_event_handler_init();
}

static void start_tasks(void) {
  // For INTERNAL: only send task is needed.
  // For SYNC: both the sync BPM task and the send task are needed.
  if (clock_source == CLOCK_SOURCE_INTERNAL) {
    if (tempo_send_task_handle == NULL) {
      BaseType_t ret = xTaskCreate(midi_tempo_send_task, "midi_tempo", 4096, NULL, TASK_PRIORITY_MIDI_TEMPO, &tempo_send_task_handle);
      if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI tempo send task");
        tempo_send_task_handle = NULL;
      }
    }
  }
  else if (clock_source == CLOCK_SOURCE_SYNC) {
    analog_input_start_sync_detection(midi_tempo_sync_pulse);
    if (sync_bpm_task_handle == NULL) {
      BaseType_t ret = xTaskCreate(sync_bpm_task, "sync_bpm", 4096, NULL, TASK_PRIORITY_SYNC_BPM, &sync_bpm_task_handle);
      if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sync BPM task");
        sync_bpm_task_handle = NULL;
      }
    }
    if (tempo_send_task_handle == NULL) {
      BaseType_t ret = xTaskCreate(midi_tempo_send_task, "midi_tempo", 4096, NULL, TASK_PRIORITY_MIDI_TEMPO, &tempo_send_task_handle);
      if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MIDI tempo send task");
        tempo_send_task_handle = NULL;
      }
    }
  }
  else if (clock_source == CLOCK_SOURCE_MIDI) {
    // No tasks needed for MIDI clock source
  }
}

void midi_tempo_start(void) {
  if (clock_source == CLOCK_SOURCE_INTERNAL || clock_source == CLOCK_SOURCE_SYNC) {
    start_tasks();
    ESP_LOGI(TAG, "MIDI tempo tasks started");
  }
  else {
    ESP_LOGI(TAG, "Clock source is MIDI; no tempo tasks started");
  }
}

static void stop_tasks(void) {
  if (clock_source == CLOCK_SOURCE_SYNC) analog_input_stop_sync_detection();
  if (sync_bpm_task_handle != NULL) {
    vTaskDelete(sync_bpm_task_handle);
    sync_bpm_task_handle = NULL;
  }
  if (tempo_send_task_handle != NULL) {
    vTaskDelete(tempo_send_task_handle);
    tempo_send_task_handle = NULL;
  }
}

void midi_tempo_stop(void) {
  stop_tasks();
  ESP_LOGI(TAG, "MIDI tempo tasks stopped");
}

void midi_tempo_set_bpm(uint16_t bpm) {
  global_bpm = bpm;
  esp_err_t ret = app_settings_save_u16(NVS_KEY_BPM, bpm);
  if (ret != ESP_OK) ESP_LOGE(TAG, "Failed to save BPM to NVS: %s", esp_err_to_name(ret));
  ESP_LOGI(TAG, "Global BPM set to %d", global_bpm);
}

uint16_t midi_tempo_get_bpm(void) {
  return global_bpm;
}

void midi_tempo_set_source(midi_clock_source_t source) {
  if (source != clock_source) stop_tasks();
  clock_source = source;
  ESP_LOGI(TAG, "Clock source set to %d", clock_source);
  
  if (clock_source == CLOCK_SOURCE_INTERNAL || clock_source == CLOCK_SOURCE_SYNC) start_tasks();
}

void midi_tempo_sync_pulse(void) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (sync_semaphore) {
    xSemaphoreGiveFromISR(sync_semaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
  }
}

void midi_tempo_enable_quarter_note_log(bool enable) {
  quarter_note_log_enabled = enable;
  ESP_LOGI(TAG, "Quarter note log %s", enable ? "enabled" : "disabled");
}

void midi_tempo_tap_event(void) {
  uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
  if (tap_count > 0 && (now - tap_timestamps[(tap_index + TAP_BUFFER_SIZE - 1) % TAP_BUFFER_SIZE]) > TAP_TIMEOUT_MS) {
    tap_count = 0;
    tap_index = 0;
  }
  tap_timestamps[tap_index] = now;
  tap_index = (tap_index + 1) % TAP_BUFFER_SIZE;
  if (tap_count < TAP_BUFFER_SIZE) tap_count++;
  if (tap_count >= 2) {
    uint32_t sum_intervals = 0;
    int intervals = tap_count - 1;
    uint32_t sorted[TAP_BUFFER_SIZE];
    int idx = tap_index;
    for (int i = 0; i < tap_count; i++) sorted[i] = tap_timestamps[(idx + i) % TAP_BUFFER_SIZE];
    for (int i = 1; i < tap_count; i++) sum_intervals += (sorted[i] - sorted[i - 1]);
    uint32_t avg_interval = sum_intervals / intervals;
    if (avg_interval > 0) {
      uint16_t new_bpm = 60000 / avg_interval;
      global_bpm = new_bpm;
      ESP_LOGI(TAG, "Tap Tempo BPM updated: %d", global_bpm);
    }
  }
}

void midi_tempo_midi_clock_tick(void) {
  tick_counter++;
  if (quarter_note_log_enabled && (tick_counter % note_divider == 0)) {
    blink_led_on_quarter_note();
    ESP_LOGI(TAG, "Note divider tick (MIDI): BPM %d", global_bpm);
  }
}

void midi_tempo_set_note_divider(midi_note_divider_t divider) {
  note_divider = divider;
  ESP_LOGI(TAG, "Note divider set to %d ticks", note_divider);
}

midi_note_divider_t midi_tempo_get_note_divider(void) {
  return (midi_note_divider_t) note_divider;
}

//------------------------------------------------------------
// LED Blink Function (Blocking version)
//------------------------------------------------------------
void blink_led_on_quarter_note(void) {
  uint16_t bpm = midi_tempo_get_bpm();
  if (bpm == 0) {
    ESP_LOGW("LED_BLINK", "BPM is 0; cannot compute LED pulse duration");
    return;
  }
  uint32_t quarter_note_ms = 60000 / bpm;
  uint32_t on_time_ms = (quarter_note_ms * LED_DEFAULT_ON_PERCENT) / 100;
  
  event_t led_event = {
    .type = EVENT_LED_FLASH_REQUEST,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.led_flash = { .duration_ms = on_time_ms }
  };
  event_bus_post(&led_event);
}
