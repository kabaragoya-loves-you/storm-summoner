#include "drv2605.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "i2c_common.h"

#define TAG "DRV2605"

static i2c_master_dev_handle_t drv2605_dev = NULL;

static esp_err_t drv2605_write_reg(uint8_t reg, uint8_t data) {
  uint8_t tx_data[2] = { reg, data };
  return i2c_master_transmit(drv2605_dev, tx_data, 2, -1);
}

esp_err_t drv2605_setup(void) {
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address   = DRV2605_ADDR,
    .scl_speed_hz = 100000,
  };

  i2c_master_bus_handle_t bus_handle;
  ESP_ERROR_CHECK(i2c_master_get_bus_handle(0, &bus_handle));

  esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &drv2605_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add DRV2605 device to bus");
    return err;
  }

  err = drv2605_write_reg(DRV2605_REG_MODE, DRV2605_MODE_INTTRIG);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set mode register");
    return err;
  }

  // Select default effect library (typically library 1)
  err = drv2605_write_reg(DRV2605_REG_LIBRARY, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set library register");
    return err;
  }

  return ESP_OK;
}

esp_err_t drv2605_set_mode(uint8_t mode) {
  return drv2605_write_reg(DRV2605_REG_MODE, mode);
}

esp_err_t drv2605_set_waveform(uint8_t slot, uint8_t waveform) {
  uint8_t reg = DRV2605_REG_WAVEFORM_SEQ1 + slot;
  return drv2605_write_reg(reg, waveform);
}

esp_err_t drv2605_go(void) {
  return drv2605_write_reg(DRV2605_REG_GO, 1);
}

esp_err_t drv2605_stop(void) {
  return drv2605_write_reg(DRV2605_REG_GO, 0);
}
