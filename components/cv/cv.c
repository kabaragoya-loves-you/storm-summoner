#include "cv.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "adc2.h"

#define TAG "CV"

static TaskHandle_t task_handle = NULL;

static int samples[MOVING_AVG_LENGTH] = {0};
static int sample_index = 0;
static int sum_samples = 0;
static int num_samples = 0;
static float cv_value = 0.0f;
static uint8_t midi_value = 0;

static void cv_task(void *arg);

void cv_init(void) {
  esp_err_t err;
  adc_oneshot_chan_cfg_t chan_config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT, // Use default bit width
    .atten = ADC_ATTEN_DB_12,           // Attenuation for proper input range
  };
  err = adc_oneshot_config_channel(adc2_handle(), CV_ADC_CHANNEL, &chan_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", err);
    return;
  }
  ESP_LOGI(TAG, "CV component initialized");
}

void cv_disable(void) {
  if (task_handle != NULL) {
    vTaskSuspend(task_handle);
    ESP_LOGI(TAG, "CV task suspended");
  }
}

void cv_enable(void) {
  if (task_handle != NULL) {
    vTaskResume(task_handle);
    ESP_LOGI(TAG, "CV task resumed");
  } else {
    BaseType_t ret = xTaskCreate(cv_task, "cv_task", 4096, NULL, 5, &task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create CV task");
      return;
    }
    ESP_LOGI(TAG, "CV task started");
  }
}

float cv_get_value(void) {
  return cv_value;
}

uint8_t cv_get_midi_value(void) {
  return midi_value;
}

static void cv_task(void *arg) {
  int raw = 0;
  while (1) {
    esp_err_t err = adc_oneshot_read(adc2_handle(), CV_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "adc_oneshot_read failed: %d", err);
    } else {
      if (num_samples < MOVING_AVG_LENGTH) {
        samples[sample_index] = raw;
        sum_samples += raw;
        num_samples++;
      } else {
        sum_samples = sum_samples - samples[sample_index] + raw;
        samples[sample_index] = raw;
      }
      sample_index = (sample_index + 1) % MOVING_AVG_LENGTH;
      int moving_avg = sum_samples / num_samples;

      cv_value = IIR_ALPHA * moving_avg + (1.0f - IIR_ALPHA) * cv_value;

      // Scale the processed value (0 - 4095) linearly to MIDI CC range (0 - 127).
      midi_value = (uint8_t)((((float)cv_value - (float)CV_MIN) * 127.0f / ((float)CV_MAX - (float)CV_MIN)) + 0.5f);
    }
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    // ESP_LOGI(TAG, "CV is %f, MIDI is %d", cv_value, midi_value);
  }
}
