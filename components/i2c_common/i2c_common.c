#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "io.h"

#define TAG "I2C_COMMON"

static i2c_master_bus_handle_t bus_handle = NULL;

i2c_master_bus_handle_t i2c_bus_handle(void) {
  if (bus_handle != NULL) return bus_handle;
  
  i2c_master_bus_config_t master_conf = {
    .sda_io_num                   = PIN_SDA,
    .scl_io_num                   = PIN_SCL,
    .i2c_port                     = I2C_NUM_0,
    .flags.enable_internal_pullup = true,
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt            = 7,
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&master_conf, &bus_handle));
  return bus_handle;
}

esp_err_t i2c_common_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data) {
  uint8_t write_buf[2] = {reg, data};
  return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

esp_err_t i2c_common_write_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data) {
  uint8_t write_buf[3] = {reg, (uint8_t)(data & 0xFF), (uint8_t)(data >> 8)};
  return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

esp_err_t i2c_common_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data) {
  return i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, -1);
}

esp_err_t i2c_common_read_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data) {
  uint8_t out_buf[2];
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, out_buf, 2, -1);
  if (ret == ESP_OK) *data = (out_buf[1] << 8) | out_buf[0];
  return ret;
}

esp_err_t i2c_common_read_block(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data, size_t len) {
  return i2c_master_transmit_receive(dev_handle, &reg, 1, data, len, -1);
}

// Big-Endian Implementations
esp_err_t i2c_common_write_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data) {
  uint8_t write_buf[3] = {reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF)};
  return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

esp_err_t i2c_common_read_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data) {
  uint8_t out_buf[2];
  esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, out_buf, 2, -1);
  if (ret == ESP_OK) *data = ((uint16_t)out_buf[0] << 8) | out_buf[1];
  return ret;
}

void i2c_common_scan(void) {
  esp_err_t ret;
  ESP_LOGI(TAG, "Scanning I2C bus...");
  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  for (uint8_t address = 1; address < 127; address++) {
    ret = i2c_master_probe(bus_handle, address, 50);
    if (ret == ESP_OK) ESP_LOGI(TAG, "Found device at I2C address 0x%02X", address);
  }
  ESP_LOGI(TAG, "I2C scan complete.");
}
