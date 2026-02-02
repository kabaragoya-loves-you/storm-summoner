#include "switch.h"
#include "i2c_common.h"
#include "io.h"
#include "revision.h"
#include "esp_log.h"

#define TAG "SWITCH"

// TMUX1113 has inverted logic on channels 2 and 3 (active-low select)
// Expression switches (P4-P7): TMUX1113 on ALL boards - P5,P6 inverted
// CV switches (P0-P3): SN74HC4066 on Rev 9 (no inversion), TMUX1113 on Rev 10+ (P2,P3 inverted)
#define EXPRESSION_INVERTED_MASK 0x60  // Bits 5,6 = 0b01100000 (always applied)
#define CV_INVERTED_MASK         0x0C  // Bits 2,3 = 0b00001100 (Rev 10+ only)

// PCA9534 configuration
#define SWITCH_I2C_ADDR        I2C_ADDR_SWITCH
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
static uint8_t s_current_mask = 0x00;  // Logical mask (what the caller requested)
static bool s_initialized = false;
static bool s_use_tmux1113_logic = false;  // Set during init based on revision

// Convert logical mask to physical mask for I2C output
// Expression (P4-P7): TMUX1113 on ALL boards - always invert P5,P6
// CV (P0-P3): SN74HC4066 on Rev 9 (no inversion), TMUX1113 on Rev 10+ (invert P1,P2)
static uint8_t logical_to_physical_mask(uint8_t logical_mask) {
  uint8_t physical = logical_mask;
  
  // Expression channels: always apply TMUX1113 inversion (all boards have TMUX1113 for expression)
  physical ^= EXPRESSION_INVERTED_MASK;
  
  // CV channels: only apply inversion on Rev 10+ (Rev 9 uses SN74HC4066 which is all active-high)
  if (s_use_tmux1113_logic) {
    physical ^= CV_INVERTED_MASK;
  }
  
  return physical;
}

void switch_init(void) {
  // Guard against double-initialization
  if (s_initialized) {
    ESP_LOGD(TAG, "Switch already initialized, skipping");
    return;
  }
  
  // Detect hardware revision for CV switch type
  // - Rev 9: CV uses SN74HC4066 (all active-high), Expression uses TMUX1113
  // - Rev 10+: Both CV and Expression use TMUX1113 (channels 2,3 are active-low)
  // Expression TMUX1113 inversion is always applied; s_use_tmux1113_logic controls CV only
  hw_revision_t rev = revision_get();
  s_use_tmux1113_logic = (rev >= HW_REV_10);  // CV inversion only on Rev 10+
  ESP_LOGI(TAG, "Switch config: Expression=TMUX1113 (P5,P6 inverted), CV=%s",
    s_use_tmux1113_logic ? "TMUX1113 (P1,P2 inverted)" : "SN74HC4066 (no inversion)");
  
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

  // Register device for debug tracking
  i2c_common_register_device(s_dev_handle, SWITCH_I2C_ADDR, "PCA9534");
  
  // Configure all pins as outputs (0 = output, 1 = input)
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_CONFIG, 0x00);
  
  // Ensure polarity is non-inverted (0 = non-inverted)
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_POLARITY, 0x00);
  
  // Set initial output state (all channels OFF)
  // For TMUX1113: inverted channels need HIGH to be OFF
  uint8_t initial_mask = logical_to_physical_mask(0x00);
  i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, initial_mask);
  
  s_initialized = true;
  s_current_mask = 0x00;  // Logical mask tracking (all OFF)
  
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
  
  // Work with logical mask (what the caller wants)
  uint8_t current_mask = s_current_mask;
  
  // Clear CV channels (bits 0-3), preserve expression channels (bits 4-7)
  uint8_t new_mask = current_mask & 0xF0;
  
  // Set the requested CV channel
  if (channel != SWITCH_CHANNEL_NONE) {
    new_mask |= (1 << channel);
  }
  
  // Convert logical mask to physical and write
  uint8_t physical_mask = logical_to_physical_mask(new_mask);
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, physical_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set CV channel %d: %s", channel, esp_err_to_name(ret));
    return false;
  }
  
  s_current_channel = channel;
  s_current_mask = new_mask;  // Store logical mask
  
  ESP_LOGD(TAG, "CV channel set to %d, logical: 0x%02X, physical: 0x%02X", 
    channel, new_mask, physical_mask);
  
  return true;
}

bool switch_set_cv_mask(uint8_t cv_mask) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Work with logical mask
  uint8_t current_mask = s_current_mask;
  
  // Clear CV channels (bits 0-3), preserve expression channels (bits 4-7)
  uint8_t new_mask = current_mask & 0xF0;
  
  // Set the requested CV channels (only bits 0-3 are valid)
  new_mask |= (cv_mask & 0x0F);
  
  // Convert logical mask to physical and write
  uint8_t physical_mask = logical_to_physical_mask(new_mask);
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, physical_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set CV mask 0x%02X: %s", cv_mask, esp_err_to_name(ret));
    return false;
  }
  
  s_current_mask = new_mask;  // Store logical mask
  
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
  
  ESP_LOGD(TAG, "CV mask set to 0x%02X, logical: 0x%02X, physical: 0x%02X", 
    cv_mask, new_mask, physical_mask);
  
  return true;
}

bool switch_set_expression_mask(uint8_t expression_mask) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Work with logical mask
  uint8_t current_mask = s_current_mask;
  
  // Clear expression channels (bits 4-7), preserve CV channels (bits 0-3)
  uint8_t new_mask = current_mask & 0x0F;
  
  // Set the requested expression channels (only bits 4-7 are valid)
  new_mask |= (expression_mask & 0xF0);
  
  // Convert logical mask to physical and write
  uint8_t physical_mask = logical_to_physical_mask(new_mask);
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, physical_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set expression mask 0x%02X: %s", expression_mask, esp_err_to_name(ret));
    return false;
  }
  
  s_current_mask = new_mask;  // Store logical mask
  
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
  
  ESP_LOGD(TAG, "Expression mask set to 0x%02X, logical: 0x%02X, physical: 0x%02X", 
    expression_mask, new_mask, physical_mask);
  
  return true;
}

bool switch_set_channels_mask(uint8_t mask) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "Switch not initialized");
    return false;
  }
  
  // Convert logical mask to physical and write
  uint8_t physical_mask = logical_to_physical_mask(mask);
  esp_err_t ret = i2c_common_write_reg(s_dev_handle, SWITCH_REG_OUTPUT, physical_mask);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set channel mask 0x%02X: %s", mask, esp_err_to_name(ret));
    return false;
  }
  
  s_current_mask = mask;  // Store logical mask
  
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
