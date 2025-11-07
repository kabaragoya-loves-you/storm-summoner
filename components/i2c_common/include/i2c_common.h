#ifndef I2C_COMMON_H
#define I2C_COMMON_H

#include "driver/i2c_master.h"
#include <stdbool.h>

i2c_master_bus_handle_t i2c_bus_handle(void);

// Debug control
void i2c_common_debug_enable(bool enable);
bool i2c_common_debug_enabled(void);
void i2c_common_register_device(i2c_master_dev_handle_t dev_handle, uint8_t address, const char *name);
void i2c_common_print_stats(void);
void i2c_common_reset_stats(void);

// Little-Endian (or byte-wise) helpers
esp_err_t i2c_common_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data);
esp_err_t i2c_common_write_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data);
esp_err_t i2c_common_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data);
esp_err_t i2c_common_read_reg16(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data);
esp_err_t i2c_common_read_block(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data, size_t len);

// Big-Endian helpers
esp_err_t i2c_common_write_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t data);
esp_err_t i2c_common_read_reg16_be(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint16_t *data);

// Utility functions
void i2c_common_scan(void);

#endif // I2C_COMMON_H
