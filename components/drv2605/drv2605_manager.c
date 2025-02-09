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

esp_err_t drv2605_enqueue_job(const haptic_job_t *job) {
  if (haptic_job_queue == NULL) {
    ESP_LOGE(TAG, "Job queue not created");
    return ESP_FAIL;
  }

  if (xQueueSend(haptic_job_queue, job, pdMS_TO_TICKS(100)) != pdPASS) {
    ESP_LOGE(TAG, "Failed to enqueue haptic job");
    return ESP_FAIL;
  }
  return ESP_OK;
}

void drv2605_start_job_task(void) {
  if (haptic_job_queue == NULL) {
    haptic_job_queue = xQueueCreate(10, sizeof(haptic_job_t));
    if (haptic_job_queue == NULL) {
      ESP_LOGE(TAG, "Failed to create haptic job queue");
      return;
    }
  }

  xTaskCreate(drv2605_job_task, "drv2605_job_task", 4096, NULL, 5, NULL);
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
      ESP_LOGI(TAG, "Received haptic job with %d steps", job.length);
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
