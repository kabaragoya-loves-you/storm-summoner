#include "expression.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "adc2.h"

#define TAG "EXPRESSION"

static TaskHandle_t task_handle = NULL;

static int samples[MOVING_AVG_LENGTH] = {0};
static int sample_index = 0;
static int sum_samples = 0;
static int num_samples = 0;
static float expression_value = 0.0f;
static uint8_t midi_value = 0;

static void expression_task(void *arg);

void expression_init(void) {
  esp_err_t err;
  adc_oneshot_chan_cfg_t chan_config = {
    .bitwidth = ADC_BITWIDTH_DEFAULT, // Use default bit width
    .atten = ADC_ATTEN_DB_12,           // Attenuation for proper input range
  };
  err = adc_oneshot_config_channel(adc2_handle(), EXPRESSION_ADC_CHANNEL, &chan_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %d", err);
    return;
  }
  ESP_LOGI(TAG, "Expression component initialized");
}

void expression_disable(void) {
  if (task_handle != NULL) {
    vTaskSuspend(task_handle);
    ESP_LOGI(TAG, "Expression task suspended");
  }
}

void expression_enable(void) {
  if (task_handle != NULL) {
    vTaskResume(task_handle);
    ESP_LOGI(TAG, "Expression task resumed");
  } else {
    if (task_handle != NULL) {
      ESP_LOGW(TAG, "Expression task already running");
      return;
    }
    BaseType_t ret = xTaskCreate(expression_task, "expression_task", 4096, NULL, 5, &task_handle);
    if (ret != pdPASS) {
      ESP_LOGE(TAG, "Failed to create Expression task");
      return;
    }
    ESP_LOGI(TAG, "Expression task started");
  }
}

float expression_get_value(void) {
  return expression_value;
}

uint8_t expression_get_midi_value(void) {
  return midi_value;
}

static void expression_task(void *arg) {
  int raw = 0;
  while (1) {
    esp_err_t err = adc_oneshot_read(adc2_handle(), EXPRESSION_ADC_CHANNEL, &raw);
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

      expression_value = IIR_ALPHA * moving_avg + (1.0f - IIR_ALPHA) * expression_value;

      // Scale the processed value (0 - 4095) linearly to MIDI CC range (0 - 127).
      midi_value = (uint8_t)((((float)expression_value - (float)EXPRESSION_MIN) * 127.0f / ((float)EXPRESSION_MAX - (float)EXPRESSION_MIN)) + 0.5f);
    }
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    // ESP_LOGI(TAG, "Expression is %f, MIDI is %d", expression_value, midi_value);
  }
}
