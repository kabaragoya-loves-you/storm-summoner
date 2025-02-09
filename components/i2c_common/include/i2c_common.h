#ifndef I2C_COMMON_H
#define I2C_COMMON_H

#include "driver/i2c_master.h"

#define I2C_MASTER_SDA_IO    17
#define I2C_MASTER_SCL_IO    18
#define I2C_MASTER_NUM       I2C_NUM_0

void i2c_common_init(void);

#endif // I2C_COMMON_H
