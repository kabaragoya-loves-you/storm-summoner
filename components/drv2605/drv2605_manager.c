#include "drv2605_manager.h"
#include "drv2605.h"
#include "i2c_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "DRV2605_MANAGER"

static QueueHandle_t haptic_job_queue = NULL;

static void drv2605_job_task(void *pvParameters);

static const haptic_job_t HAPTIC_JOBS[NUM_HAPTIC_JOBS] = {
  [SHORT_PULSE] = { .waveform_sequence = { 1, 2 }, .length = 2, .name = "SHORT_PULSE" },
  [LONG_BUZZ]   = { .waveform_sequence = { 3, 4, 5 }, .length = 3, .name = "LONG_BUZZ" },
  [RAMP_UP]     = { .waveform_sequence = { 6, 7, 8, 9 }, .length = 4, .name = "RAMP_UP" },
  [RAMP_DOWN]   = { .waveform_sequence = { 10, 11 }, .length = 2, .name = "RAMP_DOWN" }
};

void haptic(haptic_job_id_t job_id) {
  if (haptic_job_queue == NULL) {
    ESP_LOGE(TAG, "Job queue not created");
    return;
  }
  if (job_id < NUM_HAPTIC_JOBS) {
    if (xQueueSend(haptic_job_queue, &HAPTIC_JOBS[job_id], pdMS_TO_TICKS(100)) != pdPASS) {
      ESP_LOGE(TAG, "Failed to enqueue haptic job");
    }
    ESP_LOGI(TAG, "Enqueued haptic job %s with %d steps", HAPTIC_JOBS[job_id].name, HAPTIC_JOBS[job_id].length);
  }
}

void drv2605_start(void) {
  if (haptic_job_queue == NULL) {
    haptic_job_queue = xQueueCreate(10, sizeof(haptic_job_t));
    if (haptic_job_queue == NULL) {
      ESP_LOGE(TAG, "Failed to create haptic job queue");
      return;
    }
  }

  xTaskCreate(drv2605_job_task, "drv2605_job_task", 4096, NULL, 10, NULL);
}

static void drv2605_job_task(void *pvParameters) {
  haptic_job_t job;

  if (drv2605_init() != ESP_OK) {
    ESP_LOGE(TAG, "DRV2605 initialization failed in job task");
    vTaskDelete(NULL);
  }
  ESP_LOGI(TAG, "DRV2605 job task started");

  while (1) {
    if (xQueueReceive(haptic_job_queue, &job, portMAX_DELAY) == pdPASS) {
      ESP_LOGI(TAG, "Received haptic job %s with %d steps", job.name, job.length);

      // Loop through the sequence slots, adding the waveform and then a 0 to mark the end.
      for (uint8_t i = 0; i < job.length; i++) {
        if (drv2605_set_waveform(i, job.waveform_sequence[i]) != ESP_OK) {
          ESP_LOGE(TAG, "Failed to set waveform at slot %d", i);
          break;
        }
      }

      drv2605_set_waveform(job.length, 0);

      if (drv2605_go() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to trigger haptic effect");
      }

      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}
