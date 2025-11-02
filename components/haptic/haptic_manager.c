#include "haptic_manager.h"
#include "haptic.h"
#include "i2c_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "task_priorities.h"

// Forward declaration for event handler init
void haptic_event_handler_init(void);

#define TAG "HAPTIC_MANAGER"
#define HAPTIC_JOB_QUEUE_SIZE 15
#define HAPTIC_JOBS_COUNT 3

static QueueHandle_t haptic_job_queue = NULL;
static volatile uint32_t s_haptic_busy_until_ms = 0;

static void haptic_job_task(void *pvParameters);

// DRV2605 ROM waveform effect durations in milliseconds (measured/documented values)
static const uint8_t WAVEFORM_DURATIONS_MS[128] = {
  [1]  = 15,   // Strong Click - 100%
  [21] = 40,   // Pulsing Sharp 1 - 100%
  [24] = 40,   // Pulsing Sharp 2 - 100%
  // Add more as needed, default 0 for unknown
};

static const haptic_job_t HAPTIC_JOBS[HAPTIC_JOBS_COUNT] = {
  [CLICK]     = { .waveform_sequence = { 1 }, .length = 1, .name = "CLICK" },
  [INCREMENT] = { .waveform_sequence = { 21 }, .length = 1, .name = "INCREMENT" },
  [DECREMENT] = { .waveform_sequence = { 24 }, .length = 1, .name = "DECREMENT" },
};

static uint32_t calculate_job_duration_ms(const haptic_job_t *job) {
  uint32_t total_ms = 0;
  for (uint8_t i = 0; i < job->length; i++) {
    uint8_t waveform_id = job->waveform_sequence[i];
    if (waveform_id < 128) {
      total_ms += WAVEFORM_DURATIONS_MS[waveform_id];
    }
  }
  // Add small buffer for I2C communication overhead
  return total_ms > 0 ? total_ms + 5 : 50;
}

bool haptic_is_busy(void) {
  uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  return now_ms < s_haptic_busy_until_ms;
}

void haptic(haptic_job_id_t job_id) {
  if (haptic_job_queue == NULL) {
    ESP_LOGE(TAG, "Job queue not created");
    return;
  }
  
  if (job_id >= HAPTIC_JOBS_COUNT) return;
  
  // Skip if a haptic is currently playing
  if (haptic_is_busy()) {
    ESP_LOGD(TAG, "Skipping haptic %s - already busy", HAPTIC_JOBS[job_id].name);
    return;
  }
  
  // Calculate duration and reserve time slot
  uint32_t duration_ms = calculate_job_duration_ms(&HAPTIC_JOBS[job_id]);
  uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
  s_haptic_busy_until_ms = now_ms + duration_ms;
  
  if (xQueueSend(haptic_job_queue, &HAPTIC_JOBS[job_id], pdMS_TO_TICKS(100)) != pdPASS) {
    ESP_LOGE(TAG, "Failed to enqueue haptic job");
    // Reset busy flag if we couldn't queue
    s_haptic_busy_until_ms = 0;
  }
}

void haptic_init(void) {
  if (haptic_job_queue == NULL) {
    haptic_job_queue = xQueueCreate(HAPTIC_JOB_QUEUE_SIZE, sizeof(haptic_job_t));
    if (haptic_job_queue == NULL) {
      ESP_LOGE(TAG, "Failed to create haptic job queue");
      return;
    }
  }

  xTaskCreate(haptic_job_task, "haptic", 3072, NULL, TASK_PRIORITY_HAPTIC, NULL);
  ESP_LOGI(TAG, "Haptic feedback initialized");
  
  haptic_event_handler_init();
}

static void haptic_job_task(void *pvParameters) {
  haptic_job_t job;

  if (haptic_setup() != ESP_OK) {
    ESP_LOGE(TAG, "Haptic initialization failed in job task");
    vTaskDelete(NULL);
  }

  while (1) {
    if (xQueueReceive(haptic_job_queue, &job, portMAX_DELAY) == pdPASS) {
      // Loop through the sequence slots, adding the waveform and then a 0 to mark the end.
      for (uint8_t i = 0; i < job.length; i++) {
        if (haptic_set_waveform(i, job.waveform_sequence[i]) != ESP_OK) {
          ESP_LOGE(TAG, "Failed to set waveform at slot %d", i);
          break;
        }
      }

      haptic_set_waveform(job.length, 0);

      if (haptic_go() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger haptic effect");
      }

      // Wait for the actual effect duration instead of fixed 100ms
      uint32_t duration_ms = calculate_job_duration_ms(&job);
      vTaskDelay(pdMS_TO_TICKS(duration_ms));
    }
  }
}
