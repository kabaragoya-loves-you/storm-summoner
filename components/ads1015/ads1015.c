#include "ads1015.h"
#include "i2c_common.h"
#include "io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ADS1015"

// ADS1015 I2C address and registers
#define ADS1015_I2C_ADDR              0x48
#define ADS1015_REG_POINTER_CONVERT   0x00
#define ADS1015_REG_POINTER_CONFIG    0x01
#define ADS1015_REG_POINTER_LO_THRESH 0x02
#define ADS1015_REG_POINTER_HI_THRESH 0x03

// Config register bits
#define ADS1015_CONFIG_OS_SINGLE      0x8000  // Start single conversion
#define ADS1015_CONFIG_MUX_OFFSET     12       // Mux offset in config register
#define ADS1015_CONFIG_PGA_OFFSET     9        // PGA offset in config register
#define ADS1015_CONFIG_MODE_SINGLE    0x0100  // Single-shot mode
#define ADS1015_CONFIG_DR_OFFSET      5        // Data rate offset
#define ADS1015_CONFIG_COMP_MODE      0x0010  // Comparator mode (not used)
#define ADS1015_CONFIG_COMP_POL       0x0008  // Comparator polarity (not used)
#define ADS1015_CONFIG_COMP_LAT       0x0004  // Comparator latch (not used)
#define ADS1015_CONFIG_COMP_QUE       0x0003  // Comparator queue (disabled)

// Gain to full-scale voltage mapping (in mV)
static const uint16_t gain_to_mv[] = {
  6144,  // TWOTHIRDS
  4096,  // ONE
  2048,  // TWO
  1024,  // FOUR
  512,   // EIGHT
  256    // SIXTEEN
};

static i2c_master_dev_handle_t s_dev_handle = NULL;
static ads1015_rate_t s_data_rate = ADS1015_RATE_1600SPS;

esp_err_t ads1015_init(void) {
  if (s_dev_handle) return ESP_OK; // Already initialized
  
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ADS1015_I2C_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ
  };
  
  esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Test communication by reading config register
  uint16_t config;
  ret = i2c_common_read_reg16_be(s_dev_handle, ADS1015_REG_POINTER_CONFIG, &config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read config register: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "ADS1015 initialized (address 0x%02X)", ADS1015_I2C_ADDR);
  return ESP_OK;
}

int16_t ads1015_read_channel(uint8_t channel, ads1015_gain_t gain) {
  if (!s_dev_handle || channel > 3 || gain > ADS1015_GAIN_SIXTEEN) return -1;
  
  // Build config register value
  uint16_t config = ADS1015_CONFIG_OS_SINGLE |      // Start conversion
                    ADS1015_CONFIG_MODE_SINGLE |     // Single-shot mode
                    ADS1015_CONFIG_COMP_QUE;         // Disable comparator
  
  // Set channel (MUX bits)
  config |= ((4 + channel) << ADS1015_CONFIG_MUX_OFFSET);  // Single-ended input
  
  // Set gain
  config |= (gain << ADS1015_CONFIG_PGA_OFFSET);
  
  // Set data rate
  config |= (s_data_rate << ADS1015_CONFIG_DR_OFFSET);
  
  // Write config to start conversion
  esp_err_t ret = i2c_common_write_reg16_be(s_dev_handle, ADS1015_REG_POINTER_CONFIG, config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write config: %s", esp_err_to_name(ret));
    return -1;
  }
  
  // Wait for conversion to complete
  // Conversion time depends on data rate, but max is ~8ms
  vTaskDelay(pdMS_TO_TICKS(10));
  
  // Poll the OS bit to check if conversion is complete
  uint16_t status = 0;
  for (int i = 0; i < 10; i++) {
    ret = i2c_common_read_reg16_be(s_dev_handle, ADS1015_REG_POINTER_CONFIG, &status);
    if (ret == ESP_OK && (status & ADS1015_CONFIG_OS_SINGLE)) break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  
  // Read the conversion result
  uint16_t result;
  ret = i2c_common_read_reg16_be(s_dev_handle, ADS1015_REG_POINTER_CONVERT, &result);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read result: %s", esp_err_to_name(ret));
    return -1;
  }
  
  // ADS1015 returns 12-bit result left-aligned in 16-bit register
  // In single-ended mode, result is unsigned 0-4095
  uint16_t adc_value = result >> 4;
  
  // Clamp to valid 12-bit range
  if (adc_value > 4095) adc_value = 4095;
  
  return (int16_t)adc_value;
}

int16_t ads1015_read_channel_default(uint8_t channel) {
  return ads1015_read_channel(channel, ADS1015_GAIN_ONE);  // 4.096V range
}

float ads1015_raw_to_voltage(int16_t raw_value, ads1015_gain_t gain) {
  if (raw_value < 0 || gain > ADS1015_GAIN_SIXTEEN) return 0.0f;
  
  // Convert 12-bit value to voltage based on gain setting
  float lsb_size = (float)gain_to_mv[gain] / 2048.0f;  // 2048 = 2^11 for 12-bit
  return (raw_value * lsb_size) / 1000.0f;  // Convert mV to V
}

esp_err_t ads1015_set_data_rate(ads1015_rate_t rate) {
  if (rate > ADS1015_RATE_3300SPS) return ESP_ERR_INVALID_ARG;
  s_data_rate = rate;
  return ESP_OK;
}

float ads1015_read_ratiometric(uint8_t channel_num, uint8_t reference_channel, ads1015_gain_t gain) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "ADS1015 not initialized");
    return -1.0f;
  }
  
  if (channel_num > 3 || reference_channel > 3) {
    ESP_LOGE(TAG, "Invalid channel numbers: %d, %d", channel_num, reference_channel);
    return -1.0f;
  }
  
  // Read the reference channel first
  int16_t ref_value = ads1015_read_channel(reference_channel, gain);
  if (ref_value < 0) {
    ESP_LOGE(TAG, "Failed to read reference channel %d", reference_channel);
    return -1.0f;
  }
  
  // Read the measurement channel
  int16_t channel_value = ads1015_read_channel(channel_num, gain);
  if (channel_value < 0) {
    ESP_LOGE(TAG, "Failed to read channel %d", channel_num);
    return -1.0f;
  }
  
  // Avoid division by zero
  if (ref_value == 0) {
    ESP_LOGW(TAG, "Reference channel reads 0, cannot compute ratio");
    return 0.0f;
  }
  
  // Calculate ratio (0.0 to 1.0)
  float ratio = (float)channel_value / (float)ref_value;
  
  // Clamp to valid range
  if (ratio > 1.0f) ratio = 1.0f;
  if (ratio < 0.0f) ratio = 0.0f;
  
  return ratio;
}
