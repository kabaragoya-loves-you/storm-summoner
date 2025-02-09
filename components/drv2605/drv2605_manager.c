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
  [STRONG_CLICK] = { .waveform_sequence = { 1 }, .length = 1, .name = "STRONG_CLICK" },
  [SHARP_CLICK]   = { .waveform_sequence = { 4 }, .length = 1, .name = "SHARP_CLICK" },
  [SOFT_BUMP]     = { .waveform_sequence = { 7 }, .length = 1, .name = "SOFT_BUMP" },
  [DOUBLE_CLICK]   = { .waveform_sequence = { 10 }, .length = 1, .name = "DOUBLE_CLICK" },
  [TRIPLE_CLICK]   = { .waveform_sequence = { 12 }, .length = 1, .name = "TRIPLE_CLICK" },
  [STRONG_BUZZ]   = { .waveform_sequence = { 14 }, .length = 1, .name = "STRONG_BUZZ" },
  [ALERT_1000]   = { .waveform_sequence = { 16 }, .length = 1, .name = "ALERT_1000" },
  [SHARP_TICK]   = { .waveform_sequence = { 24 }, .length = 1, .name = "SHARP_TICK" },
  [SHORT_DOUBLE_CLICK_STRONG]   = { .waveform_sequence = { 27 }, .length = 1, .name = "SHORT_DOUBLE_CLICK_STRONG" },
  [LONG_DOUBLE_SHARP_CLICK_STRONG]   = { .waveform_sequence = { 37 }, .length = 1, .name = "LONG_DOUBLE_SHARP_CLICK_STRONG" },
  [BUZZ]   = { .waveform_sequence = { 47 }, .length = 1, .name = "BUZZ" },
  [PULSING_STRONG]   = { .waveform_sequence = { 52 }, .length = 1, .name = "PULSING_STRONG" },
  [TRANSITION_CLICK]   = { .waveform_sequence = { 58 }, .length = 1, .name = "TRANSITION_CLICK" }
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
