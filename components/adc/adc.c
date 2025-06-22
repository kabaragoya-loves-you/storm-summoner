#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_common.h"
#include "app_settings.h"
#include "task_priorities.h"
#include "adc.h"
#include "io.h"
#include "driver/gpio.h"
#include "midi_messages.h"
#include <math.h>

#define TAG "ADC_ADS1015"

// --- ADC Configuration ---
#define ADS1015_I2C_ADDR 0x48
#define ADS1015_REG_POINTER_CONVERT   0x00
#define ADS1015_REG_POINTER_CONFIG    0x01

// Config Register Bits
#define ADS1015_CONFIG_OS_SINGLE      0x8000
#define ADS1015_CONFIG_PGA_4_096V     0x0200
#define ADS1015_CONFIG_MODE_SINGLE    0x0100
#define ADS1015_CONFIG_DR_1600SPS     0x0080
#define MUX_OFFSET 12

// --- Expression Pedal Logic ---
#define NVS_KEY_EXP_MIN "exp_min"
#define NVS_KEY_EXP_MAX "exp_max"
#define NVS_KEY_EXP_DEADZONE "exp_deadzone"
static int16_t s_expression_min = 156;
static int16_t s_expression_max = 1609;
static uint8_t s_expression_deadzone = 1;
#define MOVING_AVG_LENGTH 10
#define IIR_ALPHA 0.3f

static i2c_master_dev_handle_t s_adc_dev_handle = NULL;
static TaskHandle_t s_expression_task_handle = NULL;
static float s_expression_value = 0.0f;
static uint8_t s_expression_midi_value = 0;

// --- CV Logic ---
#define NVS_KEY_CV_MODE "adc_cv_mode"
static TaskHandle_t s_cv_task_handle = NULL;
static adc_cv_mode_t s_cv_mode = CV_INPUT_MODE_5V;
static volatile int16_t s_cv_value = 0;


static int16_t read_adc_channel(uint8_t channel) {
  if (!s_adc_dev_handle) return 0;
  uint16_t config = ADS1015_CONFIG_OS_SINGLE | ADS1015_CONFIG_PGA_4_096V | ADS1015_CONFIG_MODE_SINGLE | ADS1015_CONFIG_DR_1600SPS;
  config |= ((4 + channel) << MUX_OFFSET);

  // Write config to select the channel, but don't start conversion yet
  uint16_t temp_config = config & ~ADS1015_CONFIG_OS_SINGLE;
  if (i2c_common_write_reg16_be(s_adc_dev_handle, ADS1015_REG_POINTER_CONFIG, temp_config) != ESP_OK) return 0;

  // Wait for the input voltage to settle after MUX switch, especially with an RC filter
  vTaskDelay(pdMS_TO_TICKS(1));

  // Now, start the conversion
  if (i2c_common_write_reg16_be(s_adc_dev_handle, ADS1015_REG_POINTER_CONFIG, config) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write config for channel %d", channel);
    return 0;
  }

  // Wait for the conversion to complete by polling the OS bit.
  uint16_t status = 0;
  for (int i = 0; i < 10; i++) { // Poll for up to 10ms
    if (i2c_common_read_reg16_be(s_adc_dev_handle, ADS1015_REG_POINTER_CONFIG, &status) == ESP_OK) {
      if ((status & ADS1015_CONFIG_OS_SINGLE) != 0) {
        // Conversion is ready, break the loop
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  
  // Read conversion result
  uint16_t result;
  if (i2c_common_read_reg16_be(s_adc_dev_handle, ADS1015_REG_POINTER_CONVERT, &result) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read result for channel %d", channel);
    return 0;
  }
  
  return (int16_t)(result >> 4);
}

static void expression_task(void *pvParameters) {
  int samples[MOVING_AVG_LENGTH] = {0};
  int sample_index = 0;
  int sum_samples = 0;
  int num_samples = 0;
  uint8_t last_midi_value = 0;

  while (1) {
    // Only run logic if cable is inserted (pin is HIGH).
    if (gpio_get_level(EXPRESSION_CABLE_DETECT_GPIO) == 1) {
      int raw = read_adc_channel(0);

      // Moving average filter
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

      // IIR filter
      s_expression_value = IIR_ALPHA * moving_avg + (1.0f - IIR_ALPHA) * s_expression_value;

      // Scale to MIDI
      float scaled = (s_expression_value - s_expression_min) * 127.0f / (s_expression_max - s_expression_min);
      if (scaled < 0) scaled = 0;
      if (scaled > 127) scaled = 127;
      s_expression_midi_value = (uint8_t)(scaled + 0.5f);

      // Send MIDI message if change exceeds deadzone
      if (abs(s_expression_midi_value - last_midi_value) >= s_expression_deadzone) {
        send_control_change(0, 4, s_expression_midi_value);
        ESP_LOGI(TAG, "Expression MIDI: %d", s_expression_midi_value);
        last_midi_value = s_expression_midi_value;
      }
      
      vTaskDelay(pdMS_TO_TICKS(10));

    } else {
      // No cable is inserted, GPIO is LOW. Reset state and wait.
      s_expression_value = 0.0f;
      s_expression_midi_value = 0;
      last_midi_value = 0;
      num_samples = 0;
      sum_samples = 0;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

static void cv_task(void *pvParameters) {
  ESP_LOGI(TAG, "CV task started");
  uint8_t current_channel = 1;
  while (1) {
  switch (s_cv_mode) {
    case CV_INPUT_MODE_5V:
    current_channel = 1;
    break;
    case CV_INPUT_MODE_10V:
    current_channel = 2;
    break;
    case CV_INPUT_MODE_5V_BIPOLAR:
    current_channel = 3;
    break;
  }
  s_cv_value = read_adc_channel(current_channel);
  ESP_LOGD(TAG, "CV (AIN%d) Raw: %d", current_channel, s_cv_value);
  vTaskDelay(pdMS_TO_TICKS(20)); // ~50Hz
  }
}

void adc_init(void) {
  uint32_t stored_mode;
  if (app_settings_load_u32(NVS_KEY_CV_MODE, &stored_mode) == ESP_OK) s_cv_mode = (adc_cv_mode_t)stored_mode;
  else app_settings_save_u32(NVS_KEY_CV_MODE, (uint32_t)s_cv_mode);

  i2c_device_config_t dev_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADS1015_I2C_ADDR, .scl_speed_hz = 400000, };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_adc_dev_handle));

  xTaskCreate(cv_task, "cv", 4096, NULL, TASK_PRIORITY_ADC_CV, &s_cv_task_handle);
  ESP_LOGI(TAG, "ADS1015 initialized");
}

void expression_init(void) {
  // Load calibration from NVS
  uint32_t stored_val;
  if (app_settings_load_u32(NVS_KEY_EXP_MIN, &stored_val) == ESP_OK) s_expression_min = stored_val;
  else app_settings_save_u32(NVS_KEY_EXP_MIN, s_expression_min);

  if (app_settings_load_u32(NVS_KEY_EXP_MAX, &stored_val) == ESP_OK) s_expression_max = stored_val;
  else app_settings_save_u32(NVS_KEY_EXP_MAX, s_expression_max);
  
  if (app_settings_load_u32(NVS_KEY_EXP_DEADZONE, &stored_val) == ESP_OK) s_expression_deadzone = stored_val;
  else app_settings_save_u32(NVS_KEY_EXP_DEADZONE, s_expression_deadzone);

  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << EXPRESSION_CABLE_DETECT_GPIO),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
  };
  gpio_config(&io_conf);

  if (!s_adc_dev_handle) {
    i2c_device_config_t dev_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ADS1015_I2C_ADDR, .scl_speed_hz = 400000, };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_adc_dev_handle));
  }

  xTaskCreate(expression_task, "expression", 4096, NULL, TASK_PRIORITY_ADC_EXP, &s_expression_task_handle);
  ESP_LOGI(TAG, "ADS1015 initialized");
}

void expression_enable(void) {
  if (s_expression_task_handle) vTaskResume(s_expression_task_handle);
}

void expression_disable(void) {
  if (s_expression_task_handle) vTaskSuspend(s_expression_task_handle);
}

float expression_get_value(void) {
  return s_expression_value;
}

uint8_t expression_get_midi_value(void) {
  return s_expression_midi_value;
}

void expression_set_min_value(int16_t value) {
  s_expression_min = value;
  app_settings_save_u32(NVS_KEY_EXP_MIN, value);
}

void expression_set_max_value(int16_t value) {
  s_expression_max = value;
  app_settings_save_u32(NVS_KEY_EXP_MAX, value);
}

void expression_set_deadzone(uint8_t deadzone) {
  s_expression_deadzone = deadzone;
  app_settings_save_u32(NVS_KEY_EXP_DEADZONE, deadzone);
}

uint8_t expression_get_deadzone(void) {
  return s_expression_deadzone;
}

void adc_set_cv_mode(adc_cv_mode_t mode) {
  s_cv_mode = mode;
  app_settings_save_u32(NVS_KEY_CV_MODE, (uint32_t)mode);
  ESP_LOGI(TAG, "CV mode set to %d", mode);
}

adc_cv_mode_t adc_get_cv_mode(void) {
  return s_cv_mode;
}

int16_t adc_get_cv_value(void) {
  return s_cv_value;
} 