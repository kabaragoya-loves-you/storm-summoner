#include "switch.h"
#include "i2c_common.h"
#include "io.h"
#include "esp_log.h"

#define TAG "SWITCH"

// PCA9536 I2C address and registers
#define PCA9536_ADDR        0x41
#define PCA9536_REG_INPUT   0x00
#define PCA9536_REG_OUTPUT  0x01
#define PCA9536_REG_POLARITY 0x02
#define PCA9536_REG_CONFIG  0x03

// Static variables
static i2c_master_dev_handle_t s_dev_handle = NULL;
static switch_channel_t s_current_channel = SWITCH_CHANNEL_NONE;

void switch_init(void) {
  // Get I2C bus handle
  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  
  // Configure device
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = PCA9536_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ
  };
  
  esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
    return;
  }
  
  // Configure all pins as outputs (0 = output, 1 = input)
  i2c_common_write_reg(s_dev_handle, PCA9536_REG_CONFIG, 0x00);
  
  // Ensure polarity is non-inverted (0 = non-inverted)
  i2c_common_write_reg(s_dev_handle, PCA9536_REG_POLARITY, 0x00);
  
  // Set all outputs LOW initially (all channels OFF)
  i2c_common_write_reg(s_dev_handle, PCA9536_REG_OUTPUT, 0x00);
  
  ESP_LOGI(TAG, "Switch initialized (PCA9536 at 0x%02X)", PCA9536_ADDR);
}

bool switch_set_channel(switch_channel_t channel) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  uint8_t output_value = 0x00;  // All LOW = all OFF
  
  // First, turn everything off (break)
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, PCA9536_REG_OUTPUT, output_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to clear outputs: %s", esp_err_to_name(ret));
    return false;
  }
  
  // Then turn on the requested channel (make)
  if (channel <= SWITCH_CHANNEL_3) {
    output_value = 1 << channel;  // Set bit for selected channel
    ret = i2c_common_write_reg(s_dev_handle, PCA9536_REG_OUTPUT, output_value);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set channel %d: %s", channel, esp_err_to_name(ret));
      return false;
    }
  } else if (channel != SWITCH_CHANNEL_NONE) {
    ESP_LOGW(TAG, "Invalid channel: %d", channel);
    return false;
  }
  
  s_current_channel = channel;
  return true;
}

switch_channel_t switch_get_channel(void) {
  return s_current_channel;
}

bool switch_all_off(void) {
  return switch_set_channel(SWITCH_CHANNEL_NONE);
}
