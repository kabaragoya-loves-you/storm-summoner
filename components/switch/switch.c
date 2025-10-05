#include "switch.h"
#include "i2c_common.h"
#include "io.h"
#include "esp_log.h"

#define TAG "SWITCH"

// I2C address depends on which IC is in use
#ifdef USE_PCA9434
#define SWITCH_I2C_ADDR        0x20  // PCA9434 with A0-A2 tied to ground
#define SWITCH_NUM_CHANNELS    8
#define SWITCH_IC_NAME         "PCA9434"
#else
#define SWITCH_I2C_ADDR        0x41  // PCA9536 fixed address
#define SWITCH_NUM_CHANNELS    4
#define SWITCH_IC_NAME         "PCA9536"
#endif

// Register addresses (same for both PCA9536 and PCA9434)
#define SWITCH_REG_INPUT    0x00
#define SWITCH_REG_OUTPUT   0x01
#define SWITCH_REG_POLARITY 0x02
#define SWITCH_REG_CONFIG   0x03

// Static variables
static i2c_master_dev_handle_t s_dev_handle = NULL;
static switch_channel_t s_current_channel = SWITCH_CHANNEL_NONE;

void switch_init(void) {
  // Get I2C bus handle
  i2c_master_bus_handle_t bus_handle = i2c_bus_handle();
  
  // Configure device
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = SWITCH_I2C_ADDR,
    .scl_speed_hz = I2C_SCL_SPEED_HZ
  };
  
  esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
    return;
  }
  
  // Configure all pins as outputs (0 = output, 1 = input)
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_CONFIG, 0x00);
  
  // Ensure polarity is non-inverted (0 = non-inverted)
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_POLARITY, 0x00);
  
  // Set all outputs LOW initially (all channels OFF)
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, 0x00);
  
  ESP_LOGI(TAG, "Switch initialized (%s at 0x%02X, %d channels)", 
           SWITCH_IC_NAME, SWITCH_I2C_ADDR, SWITCH_NUM_CHANNELS);
}

bool switch_set_channel(switch_channel_t channel) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Validate channel is within range for the selected IC
  if (channel != SWITCH_CHANNEL_NONE && channel >= SWITCH_NUM_CHANNELS) {
    ESP_LOGW(TAG, "Invalid channel %d for %s (max %d)", channel, SWITCH_IC_NAME, SWITCH_NUM_CHANNELS - 1);
    return false;
  }
  
  uint8_t output_value = 0x00;  // All LOW = all OFF
  
  // First, turn everything off (break)
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, output_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to clear outputs: %s", esp_err_to_name(ret));
    return false;
  }
  
  // Then turn on the requested channel (make)
  if (channel != SWITCH_CHANNEL_NONE) {
    output_value = 1 << channel;  // Set bit for selected channel
    ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, output_value);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to set channel %d: %s", channel, esp_err_to_name(ret));
      return false;
    }
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
