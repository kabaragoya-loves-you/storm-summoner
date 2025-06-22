#ifndef I2C_COMMON_H
#define I2C_COMMON_H

#include "driver/i2c_master.h"

i2c_master_bus_handle_t i2c_bus_handle(void);

// Little-Endian (or byte-wise) helpers
esp_err_t i2c_common_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data);
esp_err_t i2c_common_write_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data);
esp_err_t i2c_common_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data);
esp_err_t i2c_common_read_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data);
esp_err_t i2c_common_read_block(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data, size_t len);

// Big-Endian helpers
esp_err_t i2c_common_write_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data);
esp_err_t i2c_common_read_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data);

#endif // I2C_COMMON_H
