#include "i2c_common.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define TAG "I2C_COMMON"

static i2c_master_bus_handle_t bus_handle = NULL;

i2c_master_bus_handle_t i2c_bus_handle(void) {
  if (bus_handle != NULL) {
    return bus_handle;
  }
  
  i2c_master_bus_config_t master_conf = {
    .sda_io_num                   = I2C_MASTER_SDA_IO,
    .scl_io_num                   = I2C_MASTER_SCL_IO,
    .i2c_port                     = I2C_MASTER_NUM,
    .flags.enable_internal_pullup = true,
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt            = 7,
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&master_conf, &bus_handle));
  return bus_handle;
}
