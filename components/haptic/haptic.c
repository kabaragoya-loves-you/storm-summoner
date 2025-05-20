#include "haptic.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "i2c_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "HAPTIC"

#define HAPTIC_SETUP_RETRY_COUNT 3
#define HAPTIC_SETUP_RETRY_DELAY_MS 100

static i2c_master_dev_handle_t haptic_dev = NULL;

static esp_err_t haptic_write_reg(uint8_t reg, uint8_t data) {
  uint8_t tx_data[2] = { reg, data };
  return i2c_master_transmit(haptic_dev, tx_data, 2, -1);
}

esp_err_t haptic_setup(void) {
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address   = HAPTIC_ADDR,
    .scl_speed_hz = 100000,
  };

  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  if (!bus_handle) {
    ESP_LOGE(TAG, "I2C bus handle is NULL");
    return ESP_FAIL;
  }

  esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &haptic_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add haptic device to bus");
    return err;
  }

  // Retry mechanism for setting mode register
  err = ESP_FAIL; // Initialize err to a failing state
  for (int i = 0; i < HAPTIC_SETUP_RETRY_COUNT; ++i) {
    err = haptic_write_reg(HAPTIC_REG_MODE, HAPTIC_MODE_INTTRIG);
    if (err == ESP_OK) {
      break; // Success
    }
    ESP_LOGW(TAG, "Failed to set mode register (attempt %d/%d), retrying in %dms...", i + 1, HAPTIC_SETUP_RETRY_COUNT, HAPTIC_SETUP_RETRY_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(HAPTIC_SETUP_RETRY_DELAY_MS));
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mode register after %d attempts", HAPTIC_SETUP_RETRY_COUNT);
    // Optional: Consider cleaning up the added device if this is critical
    // i2c_master_bus_rm_device(haptic_dev);
    // haptic_dev = NULL;
    return err;
  }
  ESP_LOGI(TAG, "Mode register set successfully.");

  // Select default effect library (typically library 1)
  err = haptic_write_reg(HAPTIC_REG_LIBRARY, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set library register");
    return err;
  }

  return ESP_OK;
}

esp_err_t haptic_set_mode(uint8_t mode) {
  return haptic_write_reg(HAPTIC_REG_MODE, mode);
}

esp_err_t haptic_set_waveform(uint8_t slot, uint8_t waveform) {
  uint8_t reg = HAPTIC_REG_WAVEFORM_SEQ1 + slot;
  return haptic_write_reg(reg, waveform);
}

esp_err_t haptic_go(void) {
  return haptic_write_reg(HAPTIC_REG_GO, 1);
}

esp_err_t haptic_stop(void) {
  return haptic_write_reg(HAPTIC_REG_GO, 0);
}
