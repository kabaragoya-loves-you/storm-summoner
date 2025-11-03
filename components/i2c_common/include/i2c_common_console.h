#ifndef I2C_COMMON_CONSOLE_H
#define I2C_COMMON_CONSOLE_H

#include "esp_err.h"

esp_err_t i2c_common_console_init(void);
void i2c_common_console_cleanup(void);

#endif // I2C_COMMON_CONSOLE_H

