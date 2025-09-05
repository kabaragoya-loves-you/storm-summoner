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
#define HAPTIC_JOB_QUEUE_SIZE 10
#define HAPTIC_JOBS_COUNT 3

static QueueHandle_t haptic_job_queue = NULL;

static void haptic_job_task(void *pvParameters);

static const haptic_job_t HAPTIC_JOBS[HAPTIC_JOBS_COUNT] = {
  [CLICK]     = { .waveform_sequence = { 1 }, .length = 1, .name = "CLICK" },
  [INCREMENT] = { .waveform_sequence = { 21 }, .length = 1, .name = "INCREMENT" },
  [DECREMENT] = { .waveform_sequence = { 24 }, .length = 1, .name = "DECREMENT" },
};

void haptic(haptic_job_id_t job_id) {
  if (haptic_job_queue == NULL) {
    ESP_LOGE(TAG, "Job queue not created");
    return;
  }
  if (job_id < HAPTIC_JOBS_COUNT) {
    if (xQueueSend(haptic_job_queue, &HAPTIC_JOBS[job_id], pdMS_TO_TICKS(100)) != pdPASS) {
      ESP_LOGE(TAG, "Failed to enqueue haptic job");
    }
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

  xTaskCreate(haptic_job_task, "haptic", 4096, NULL, TASK_PRIORITY_HAPTIC, NULL);
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
      // ESP_LOGI(TAG, "Received haptic job %s with %d steps", job.name, job.length);

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

      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
