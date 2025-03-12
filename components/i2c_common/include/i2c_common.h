#ifndef I2C_COMMON_H
#define I2C_COMMON_H

#include "driver/i2c_master.h"

#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_SCL_IO    18
#define I2C_MASTER_NUM       I2C_NUM_0

i2c_master_bus_handle_t i2c_bus_handle(void);

#endif // I2C_COMMON_H
