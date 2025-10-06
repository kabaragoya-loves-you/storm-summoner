#include "mcp4725.h"
#include "i2c_common.h"
#include "io.h"
#include "app_settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MCP4725"
#define NVS_KEY_CV_RANGE "cv_range"

// MCP4725 command bits
#define MCP4725_CMD_WRITE_DAC      0x40  // Write DAC register (fast mode)
#define MCP4725_CMD_WRITE_DAC_EEPROM 0x60  // Write DAC and EEPROM

// Maximum DAC value (12-bit)
#define MCP4725_MAX_VALUE 4095

// DAC reference voltage (VDD)
#define MCP4725_VREF 3.3f

// DAC values for each CV range mode (calculated for 3.3V VDD)
// Values = (target_voltage / 3.3V) * 4095, rounded to nearest integer
static const uint16_t cv_range_values[] = {
  1757,  // MCP4725_RANGE_10V_BIPOLAR:  1.416V
  1540,  // MCP4725_RANGE_5V_BIPOLAR:   1.241V
  3080,  // MCP4725_RANGE_10V_UNIPOLAR: 2.481V
  2461,  // MCP4725_RANGE_5V_UNIPOLAR:  1.983V
  2048   // MCP4725_RANGE_3V3_UNIPOLAR: 1.650V
};

static i2c_master_dev_handle_t s_dev_handle = NULL;
static mcp4725_cv_range_t s_current_cv_range = MCP4725_RANGE_5V_BIPOLAR;

esp_err_t mcp4725_init(void) {
  if (s_dev_handle) {
    ESP_LOGW(TAG, "MCP4725 already initialized");
    return ESP_OK;
  }

  i2c_common_scan();

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x60,
    .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };

  esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add MCP4725 device to I2C bus: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "MCP4725 initialized at address 0x60");

  // Try to load CV range from NVS
  uint8_t stored_range;
  ret = app_settings_load_u8(NVS_KEY_CV_RANGE, &stored_range);
  
  if (ret == ESP_OK && stored_range <= MCP4725_RANGE_3V3_UNIPOLAR) {
    // Valid stored range found
    s_current_cv_range = (mcp4725_cv_range_t)stored_range;
    ESP_LOGI(TAG, "Loaded CV range from NVS: %d", stored_range);
  } else {
    // No stored value or invalid, default to 5V bipolar
    s_current_cv_range = MCP4725_RANGE_5V_BIPOLAR;
    ESP_LOGI(TAG, "No valid CV range in NVS, defaulting to 5V bipolar");
    
    // Store the default value
    ret = mcp4725_set_cv_range(s_current_cv_range);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Failed to store default CV range: %s", esp_err_to_name(ret));
  }

  // Set the DAC to the stored/default value
  uint16_t dac_value = cv_range_values[s_current_cv_range];
  ret = mcp4725_set_value(dac_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set initial DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "DAC set to range mode %d, value %u (%.3fV)", 
    s_current_cv_range, (unsigned)dac_value, 
    mcp4725_value_to_voltage(dac_value, MCP4725_VREF));

  return ESP_OK;
}

bool mcp4725_is_initialized(void) {
  return (s_dev_handle != NULL);
}

esp_err_t mcp4725_set_value(uint16_t value) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Clamp value to 12-bit range
  if (value > MCP4725_MAX_VALUE) {
    ESP_LOGW(TAG, "Value %u exceeds max, clamping to %d", (unsigned)value, MCP4725_MAX_VALUE);
    value = MCP4725_MAX_VALUE;
  }

  // Fast write mode: 2 bytes
  // Byte 0: Command (0x40) + power-down bits (normal operation = 0) + upper 4 bits of value
  // Byte 1: Lower 8 bits of value
  uint8_t data[2];
  data[0] = MCP4725_CMD_WRITE_DAC | ((value >> 8) & 0x0F);
  data[1] = value & 0xFF;

  esp_err_t ret = i2c_master_transmit(s_dev_handle, data, sizeof(data), -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

esp_err_t mcp4725_set_value_eeprom(uint16_t value, mcp4725_power_down_t power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Clamp value to 12-bit range
  if (value > MCP4725_MAX_VALUE) {
    ESP_LOGW(TAG, "Value %u exceeds max, clamping to %d", (unsigned)value, MCP4725_MAX_VALUE);
    value = MCP4725_MAX_VALUE;
  }

  // Write to DAC and EEPROM: 3 bytes
  // Byte 0: Command (0x60) + power-down bits
  // Byte 1: Upper 8 bits of value
  // Byte 2: Lower 4 bits of value (in upper nibble)
  uint8_t data[3];
  data[0] = MCP4725_CMD_WRITE_DAC_EEPROM | ((power_down & 0x03) << 1);
  data[1] = (value >> 4) & 0xFF;
  data[2] = (value << 4) & 0xF0;

  esp_err_t ret = i2c_master_transmit(s_dev_handle, data, sizeof(data), -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write DAC and EEPROM: %s", esp_err_to_name(ret));
    return ret;
  }

  // EEPROM write takes up to 50ms to complete
  vTaskDelay(pdMS_TO_TICKS(60));

  ESP_LOGI(TAG, "Wrote value %u to DAC and EEPROM", (unsigned)value);
  return ESP_OK;
}

esp_err_t mcp4725_get_value(uint16_t *value) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!value) {
    ESP_LOGE(TAG, "NULL pointer passed to mcp4725_get_value");
    return ESP_ERR_INVALID_ARG;
  }

  // Read 5 bytes from MCP4725
  // Byte 0: Status bits
  // Byte 1: DAC register high byte (upper 8 bits of 12-bit value)
  // Byte 2: DAC register low byte (lower 4 bits in upper nibble)
  // Byte 3: EEPROM high byte
  // Byte 4: EEPROM low byte
  uint8_t data[5];
  esp_err_t ret = i2c_master_receive(s_dev_handle, data, sizeof(data), -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  // Extract current DAC value from bytes 1 and 2
  *value = ((uint16_t)data[1] << 4) | (data[2] >> 4);

  return ESP_OK;
}

esp_err_t mcp4725_get_eeprom_value(uint16_t *value, mcp4725_power_down_t *power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!value) {
    ESP_LOGE(TAG, "NULL pointer passed to mcp4725_get_eeprom_value");
    return ESP_ERR_INVALID_ARG;
  }

  // Read 5 bytes from MCP4725
  uint8_t data[5];
  esp_err_t ret = i2c_master_receive(s_dev_handle, data, sizeof(data), -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read EEPROM value: %s", esp_err_to_name(ret));
    return ret;
  }

  // Extract EEPROM value from bytes 3 and 4
  *value = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];

  // Extract power-down mode from byte 3 if requested
  if (power_down) {
    *power_down = (data[3] >> 5) & 0x03;
  }

  return ESP_OK;
}

esp_err_t mcp4725_set_power_down(mcp4725_power_down_t power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  // Fast write mode with power-down bits set, value = 0
  uint8_t data[2];
  data[0] = MCP4725_CMD_WRITE_DAC | ((power_down & 0x03) << 4);
  data[1] = 0x00;

  esp_err_t ret = i2c_master_transmit(s_dev_handle, data, sizeof(data), -1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set power-down mode: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

uint16_t mcp4725_voltage_to_value(float voltage, float vref) {
  if (voltage < 0.0f) voltage = 0.0f;
  if (voltage > vref) voltage = vref;
  
  float ratio = voltage / vref;
  uint16_t value = (uint16_t)(ratio * MCP4725_MAX_VALUE + 0.5f);  // Round to nearest
  
  if (value > MCP4725_MAX_VALUE) value = MCP4725_MAX_VALUE;
  
  return value;
}

float mcp4725_value_to_voltage(uint16_t value, float vref) {
  if (value > MCP4725_MAX_VALUE) value = MCP4725_MAX_VALUE;
  
  return ((float)value / MCP4725_MAX_VALUE) * vref;
}

esp_err_t mcp4725_set_cv_range(mcp4725_cv_range_t range) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "MCP4725 not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (range > MCP4725_RANGE_3V3_UNIPOLAR) {
    ESP_LOGE(TAG, "Invalid CV range: %d", range);
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t dac_value = cv_range_values[range];

  // Set the DAC output (volatile)
  esp_err_t ret = mcp4725_set_value(dac_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  // Write to EEPROM for persistence across power cycles
  ret = mcp4725_set_value_eeprom(dac_value, MCP4725_PD_NORMAL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write DAC EEPROM: %s", esp_err_to_name(ret));
    return ret;
  }

  // Store the range enum in NVS
  ret = app_settings_save_u8(NVS_KEY_CV_RANGE, (uint8_t)range);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save CV range to NVS: %s", esp_err_to_name(ret));
    return ret;
  }

  s_current_cv_range = range;
  
  ESP_LOGI(TAG, "Set CV range to mode %d: DAC=%u (%.3fV)", 
    range, (unsigned)dac_value, mcp4725_value_to_voltage(dac_value, MCP4725_VREF));

  return ESP_OK;
}

esp_err_t mcp4725_get_cv_range(mcp4725_cv_range_t *range) {
  if (!range) {
    ESP_LOGE(TAG, "NULL pointer passed to mcp4725_get_cv_range");
    return ESP_ERR_INVALID_ARG;
  }

  *range = s_current_cv_range;
  return ESP_OK;
}
