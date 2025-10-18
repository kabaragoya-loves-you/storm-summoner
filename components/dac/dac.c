#include "dac.h"
#include "i2c_common.h"
#include "io.h"
#include "app_settings.h"
#include "adc_manager.h"
#include "hal/adc_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "DAC"
#define NVS_KEY_CV_RANGE "cv_range"

// MCP4725 command bits
#define MCP4725_CMD_WRITE_DAC      0x40  // Write DAC register (fast mode)
#define MCP4725_CMD_WRITE_DAC_EEPROM 0x60  // Write DAC and EEPROM

// Maximum DAC value (12-bit)
#define MCP4725_MAX_VALUE 4095

// DAC reference voltage (VDD) - can be updated via calibration
static float s_vref = 3.3f;  // Default assumed value

// ADC reference channel for VCC measurement (same as expression pedal)
#define REF_ADC_CHANNEL ADC_CHANNEL_2  // GPIO18 = ADC1_CH2
#define REF_ADC_ATTEN   ADC_ATTEN_DB_12

// ADC calibration correction factor (multiply measured voltage by this)
// The ESP32-P4 ADC tends to read slightly high at the top of its range
// Adjust this value to fine-tune DAC output accuracy
#define ADC_VREF_CORRECTION 0.970f  // 3.14V / 3.24V based on empirical measurement

// Target voltages for each CV range mode (ideal reference voltages)
// Order matches mcp4725_cv_range_t enum
static const float cv_range_target_voltages[] = {
  1.416f,  // MCP4725_RANGE_BIPOLAR_10V (switch ch 0)
  2.481f,  // MCP4725_RANGE_10V         (switch ch 1)
  1.241f,  // MCP4725_RANGE_BIPOLAR_5V  (switch ch 1)
  1.983f,  // MCP4725_RANGE_5V          (switch ch 2)
  1.650f   // MCP4725_RANGE_3V3         (switch ch 3)
};

// DAC codes calculated for ideal 3.3V VDD (used during initialization)
// These provide reasonable output until calibration runs
// Values = (target_voltage / 3.3V) * 4095, rounded to nearest integer
static const uint16_t cv_range_initial_codes[] = {
  1757,  // MCP4725_RANGE_BIPOLAR_10V: 1.416V @ 3.3V
  3080,  // MCP4725_RANGE_10V:         2.481V @ 3.3V
  1540,  // MCP4725_RANGE_BIPOLAR_5V:  1.241V @ 3.3V
  2461,  // MCP4725_RANGE_5V:          1.983V @ 3.3V
  2048   // MCP4725_RANGE_3V3:         1.650V @ 3.3V
};

static i2c_master_dev_handle_t s_dev_handle = NULL;
static mcp4725_cv_range_t s_current_cv_range = MCP4725_RANGE_5V;

esp_err_t dac_init(void) {
  if (s_dev_handle) {
    ESP_LOGW(TAG, "DAC already initialized");
    return ESP_OK;
  }

  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x61,
    .scl_speed_hz = I2C_SCL_SPEED_HZ,
  };

  esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add MCP4725 device to I2C bus: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "MCP4725 DAC initialized at address 0x61");

  // Try to load CV range from NVS
  uint8_t stored_range;
  ret = app_settings_load_u8(NVS_KEY_CV_RANGE, &stored_range);
  
  if (ret == ESP_OK && stored_range <= MCP4725_RANGE_3V3) {
    // Valid stored range found
    s_current_cv_range = (mcp4725_cv_range_t)stored_range;
    ESP_LOGI(TAG, "Loaded CV range from NVS: %d", stored_range);
  } else {
    // No stored value or invalid, default to unipolar 5V
    s_current_cv_range = MCP4725_RANGE_5V;
    ESP_LOGI(TAG, "No valid CV range in NVS, defaulting to unipolar 5V");
    
    // Store the default value
    ret = dac_set_cv_range(s_current_cv_range);
    if (ret != ESP_OK) ESP_LOGW(TAG, "Failed to store default CV range: %s", esp_err_to_name(ret));
  }

  // Use initial DAC code (assumes 3.3V VDD for fast boot)
  uint16_t dac_value = cv_range_initial_codes[s_current_cv_range];
  ret = dac_set_value(dac_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set initial DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "DAC initialized: range mode %d, value %u (%.3fV @ VDD=%.2fV assumed)", 
    s_current_cv_range, (unsigned)dac_value, 
    dac_value_to_voltage(dac_value, 3.3f), 3.3f);

  return ESP_OK;
}

bool dac_is_initialized(void) {
  return (s_dev_handle != NULL);
}

esp_err_t dac_set_value(uint16_t value) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
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

esp_err_t dac_set_value_eeprom(uint16_t value, mcp4725_power_down_t power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
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

esp_err_t dac_get_value(uint16_t *value) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
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

esp_err_t dac_get_eeprom_value(uint16_t *value, mcp4725_power_down_t *power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
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

esp_err_t dac_debug_readback(void) {
  uint16_t ram_value = 0, eeprom_value = 0;
  mcp4725_power_down_t pd_mode;
  
  // Read current DAC register (RAM)
  esp_err_t ret = dac_get_value(&ram_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read DAC RAM value");
    return ret;
  }
  
  // Read EEPROM value
  ret = dac_get_eeprom_value(&eeprom_value, &pd_mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read EEPROM value");
    return ret;
  }
  
  // Calculate expected voltages with current VDD
  float ram_voltage = dac_value_to_voltage(ram_value, s_vref);
  float eeprom_voltage = dac_value_to_voltage(eeprom_value, s_vref);
  
  ESP_LOGI(TAG, "=== DAC Debug Readback ===");
  ESP_LOGI(TAG, "VDD: %.2fV (calibrated)", s_vref);
  ESP_LOGI(TAG, "DAC RAM:    %u (0x%03X) -> %.3fV expected", (unsigned)ram_value, ram_value, ram_voltage);
  ESP_LOGI(TAG, "DAC EEPROM: %u (0x%03X) -> %.3fV expected", (unsigned)eeprom_value, eeprom_value, eeprom_voltage);
  ESP_LOGI(TAG, "Power-down mode: %d", pd_mode);
  ESP_LOGI(TAG, "========================");
  
  return ESP_OK;
}

esp_err_t dac_set_power_down(mcp4725_power_down_t power_down) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
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

uint16_t dac_voltage_to_value(float voltage, float vref) {
  if (voltage < 0.0f) voltage = 0.0f;
  if (voltage > vref) voltage = vref;
  
  float ratio = voltage / vref;
  uint16_t value = (uint16_t)(ratio * MCP4725_MAX_VALUE + 0.5f);  // Round to nearest
  
  if (value > MCP4725_MAX_VALUE) value = MCP4725_MAX_VALUE;
  
  return value;
}

float dac_value_to_voltage(uint16_t value, float vref) {
  if (value > MCP4725_MAX_VALUE) value = MCP4725_MAX_VALUE;
  
  return ((float)value / MCP4725_MAX_VALUE) * vref;
}

esp_err_t dac_set_cv_range(mcp4725_cv_range_t range) {
  if (!s_dev_handle) {
    ESP_LOGE(TAG, "DAC not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (range > MCP4725_RANGE_3V3) {
    ESP_LOGE(TAG, "Invalid CV range: %d", range);
    return ESP_ERR_INVALID_ARG;
  }

  // Calculate DAC value from target voltage and calibrated VREF
  float target_voltage = cv_range_target_voltages[range];
  uint16_t dac_value = dac_voltage_to_value(target_voltage, s_vref);

  // Set the DAC output (volatile)
  esp_err_t ret = dac_set_value(dac_value);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set DAC value: %s", esp_err_to_name(ret));
    return ret;
  }

  // Write to EEPROM for persistence across power cycles
  ret = dac_set_value_eeprom(dac_value, MCP4725_PD_NORMAL);
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
  
  ESP_LOGI(TAG, "Set CV range to mode %d: DAC=%u (%.3fV @ VDD=%.2fV)",
    range, (unsigned)dac_value, dac_value_to_voltage(dac_value, s_vref), s_vref);

  return ESP_OK;
}

esp_err_t dac_get_cv_range(mcp4725_cv_range_t *range) {
  if (!range) {
    ESP_LOGE(TAG, "NULL pointer passed to mcp4725_get_cv_range");
    return ESP_ERR_INVALID_ARG;
  }

  *range = s_current_cv_range;
  return ESP_OK;
}

esp_err_t dac_calibrate_vref(void) {
  // Wait for VCC to stabilize after power-on and all peripherals initializing
  ESP_LOGI(TAG, "Waiting for VCC to stabilize...");
  vTaskDelay(pdMS_TO_TICKS(200));  // 200ms delay for VCC to settle
  
  // Ensure the reference channel is registered with ADC manager
  // (It should already be registered by expression component, but check anyway)
  esp_err_t ret = adc_manager_register_channel(REF_ADC_CHANNEL, REF_ADC_ATTEN);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to register reference ADC channel: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Take multiple samples and average for accuracy
  const int num_samples = 16;
  int32_t sum_mv = 0;
  int successful_reads = 0;
  
  for (int i = 0; i < num_samples; i++) {
    int voltage_mv = 0;
    ret = adc_manager_read_calibrated(REF_ADC_CHANNEL, &voltage_mv);
    if (ret == ESP_OK) {
      sum_mv += voltage_mv;
      successful_reads++;
    }
    vTaskDelay(pdMS_TO_TICKS(2));  // Small delay between samples
  }
  
  if (successful_reads == 0) {
    ESP_LOGE(TAG, "Failed to read reference ADC for VREF calibration");
    return ESP_FAIL;
  }
  
  // Calculate average voltage in mV, then convert to V
  int avg_mv = sum_mv / successful_reads;
  float measured_vcc = (avg_mv / 1000.0f) * ADC_VREF_CORRECTION;
  
  // Sanity check: VCC should be between 2.5V and 3.8V
  if (measured_vcc < 2.5f || measured_vcc > 3.8f) {
    ESP_LOGW(TAG, "VREF calibration result out of range: %.2fV (%dmV), keeping default %.2fV", 
      measured_vcc, avg_mv, s_vref);
    return ESP_ERR_INVALID_STATE;
  }
  
  float old_vref = s_vref;
  s_vref = measured_vcc;
  
  ESP_LOGI(TAG, "VREF calibrated: %.2fV → %.2fV (%dmV raw, corrected by %.3f, %d samples)", 
    old_vref, s_vref, avg_mv, ADC_VREF_CORRECTION, successful_reads);
  
  // Recalculate and update current DAC value with measured VREF
  float target_voltage = cv_range_target_voltages[s_current_cv_range];
  uint16_t new_dac_value = dac_voltage_to_value(target_voltage, s_vref);
  uint16_t old_dac_value = cv_range_initial_codes[s_current_cv_range];
  
  if (new_dac_value != old_dac_value) {
    ret = dac_set_value(new_dac_value);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Updated DAC: %u → %u (%.3fV target)", 
        old_dac_value, new_dac_value, target_voltage);
    } else {
      ESP_LOGW(TAG, "Failed to update DAC after calibration: %s", esp_err_to_name(ret));
    }
  }
  
  return ESP_OK;
}

float dac_get_vref(void) {
  return s_vref;
}
