#include "haptic.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "i2c_common.h"

#define TAG "HAPTIC"

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

  err = haptic_write_reg(HAPTIC_REG_MODE, HAPTIC_MODE_INTTRIG);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mode register");
    return err;
  }

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
