#include "switch.h"
#include "i2c_common.h"
#include "io.h"
#include "esp_log.h"

#define TAG "SWITCH"

// PCA9534 configuration
#define SWITCH_I2C_ADDR        0x20  // PCA9534 with A0-A2 tied to ground
#define SWITCH_NUM_CHANNELS    8
#define SWITCH_IC_NAME         "PCA9534"

// Register addresses
#define SWITCH_REG_INPUT    0x00
#define SWITCH_REG_OUTPUT   0x01
#define SWITCH_REG_POLARITY 0x02
#define SWITCH_REG_CONFIG   0x03

// Static variables
static i2c_master_dev_handle_t s_dev_handle = NULL;
static switch_channel_t s_current_channel = SWITCH_CHANNEL_NONE;
static uint8_t s_current_mask = 0x00;
static bool s_initialized = false;

void switch_init(void) {
  // Guard against double-initialization
  if (s_initialized) {
    ESP_LOGD(TAG, "Switch already initialized, skipping");
    return;
  }
  
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
  
  s_initialized = true;
  s_current_mask = 0x00;  // Reset mask tracking
  
  ESP_LOGI(TAG, "Switch initialized (%s at 0x%02X, %d channels)", 
           SWITCH_IC_NAME, SWITCH_I2C_ADDR, SWITCH_NUM_CHANNELS);
}

bool switch_set_channel(switch_channel_t channel) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Validate channel is within range (CV uses channels 0-3)
  if (channel != SWITCH_CHANNEL_NONE && channel >= 4) {
    ESP_LOGW(TAG, "Invalid CV channel %d (must be 0-3)", channel);
    return false;
  }
  
  // Read current state to preserve expression channels (4-7)
  uint8_t current_mask = s_current_mask;
  
  // Clear CV channels (bits 0-3), preserve expression channels (bits 4-7)
  uint8_t new_mask = current_mask & 0xF0;
  
  // Set the requested CV channel
  if (channel != SWITCH_CHANNEL_NONE) {
    new_mask |= (1 << channel);
  }
  
  // Write the new mask
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, new_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set CV channel %d: %s", channel, esp_err_to_name(ret));
    return false;
  }
  
  s_current_channel = channel;
  s_current_mask = new_mask;
  
  ESP_LOGD(TAG, "CV channel set to %d, full mask: 0x%02X", channel, new_mask);
  
  return true;
}

bool switch_set_expression_mask(uint8_t expression_mask) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Read current state to preserve CV channels (0-3)
  uint8_t current_mask = s_current_mask;
  
  // Clear expression channels (bits 4-7), preserve CV channels (bits 0-3)
  uint8_t new_mask = current_mask & 0x0F;
  
  // Set the requested expression channels (only bits 4-7 are valid)
  new_mask |= (expression_mask & 0xF0);
  
  // Write the new mask
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, new_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set expression mask 0x%02X: %s", expression_mask, esp_err_to_name(ret));
    return false;
  }
  
  s_current_mask = new_mask;
  
  // Update s_current_channel (only meaningful if single CV channel active)
  int cv_bits = new_mask & 0x0F;
  if (cv_bits == 0) {
    s_current_channel = SWITCH_CHANNEL_NONE;
  } else {
    // Check if exactly one CV bit is set
    int bit_count = 0;
    int single_channel = -1;
    for (int i = 0; i < 4; i++) {
      if (cv_bits & (1 << i)) {
        bit_count++;
        single_channel = i;
      }
    }
    s_current_channel = (bit_count == 1) ? (switch_channel_t)single_channel : SWITCH_CHANNEL_NONE;
  }
  
  ESP_LOGD(TAG, "Expression mask set to 0x%02X, full mask: 0x%02X", expression_mask, new_mask);
  
  return true;
}

bool switch_set_channels_mask(uint8_t mask) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Write the mask directly to output register
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set channel mask 0x%02X: %s", mask, esp_err_to_name(ret));
    return false;
  }
  
  s_current_mask = mask;
  
  // Update s_current_channel for compatibility
  // If exactly one bit is set, store that channel, otherwise NONE
  int bit_count = 0;
  int single_channel = -1;
  for (int i = 0; i < 8; i++) {
    if (mask & (1 << i)) {
      bit_count++;
      single_channel = i;
    }
  }
  
  s_current_channel = (bit_count == 1) ? (switch_channel_t)single_channel : SWITCH_CHANNEL_NONE;
  
  return true;
}

switch_channel_t switch_get_channel(void) {
  return s_current_channel;
}

uint8_t switch_get_channels_mask(void) {
  return s_current_mask;
}

bool switch_all_off(void) {
  return switch_set_channel(SWITCH_CHANNEL_NONE);
}
