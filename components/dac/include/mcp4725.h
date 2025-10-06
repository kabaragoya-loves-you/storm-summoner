#ifndef _MCP4725_H
#define _MCP4725_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// CV output voltage range modes
typedef enum {
  MCP4725_RANGE_10V_BIPOLAR = 0,   // ±10V range, 1.416V DAC output
  MCP4725_RANGE_5V_BIPOLAR = 1,    // ±5V range, 1.241V DAC output
  MCP4725_RANGE_10V_UNIPOLAR = 2,  // 0-10V range, 2.481V DAC output
  MCP4725_RANGE_5V_UNIPOLAR = 3,   // 0-5V range, 1.983V DAC output
  MCP4725_RANGE_3V3_UNIPOLAR = 4   // 0-3.3V range, 1.650V DAC output
} mcp4725_cv_range_t;

// MCP4725 power-down modes
typedef enum {
  MCP4725_PD_NORMAL = 0,        // Normal operation
  MCP4725_PD_1K_GND = 1,        // Power-down with 1kΩ to GND
  MCP4725_PD_100K_GND = 2,      // Power-down with 100kΩ to GND
  MCP4725_PD_500K_GND = 3       // Power-down with 500kΩ to GND
} mcp4725_power_down_t;

/**
 * Initialize the MCP4725 DAC
 * Reads the CV range from NVS and sets the DAC to the stored value.
 * If no value is stored, defaults to 5V bipolar mode.
 * @return ESP_OK on success
 */
esp_err_t mcp4725_init(void);

/**
 * Check if MCP4725 is initialized
 * @return true if initialized, false otherwise
 */
bool mcp4725_is_initialized(void);

/**
 * Set the DAC output value (fast write, volatile)
 * This writes only to the DAC register. Value is lost on power-off.
 * @param value 12-bit DAC value (0-4095)
 * @return ESP_OK on success
 */
esp_err_t mcp4725_set_value(uint16_t value);

/**
 * Set the DAC output value and save to EEPROM (persistent)
 * This writes to both the DAC register and EEPROM. Value persists after power-off.
 * Note: EEPROM has limited write cycles (~100k), so use sparingly.
 * @param value 12-bit DAC value (0-4095)
 * @param power_down Power-down mode (typically MCP4725_PD_NORMAL)
 * @return ESP_OK on success
 */
esp_err_t mcp4725_set_value_eeprom(uint16_t value, mcp4725_power_down_t power_down);

/**
 * Get the current DAC output value from the DAC register
 * @param value Pointer to store the current 12-bit DAC value
 * @return ESP_OK on success
 */
esp_err_t mcp4725_get_value(uint16_t *value);

/**
 * Get the value stored in EEPROM
 * @param value Pointer to store the 12-bit EEPROM value
 * @param power_down Pointer to store the power-down mode (can be NULL if not needed)
 * @return ESP_OK on success
 */
esp_err_t mcp4725_get_eeprom_value(uint16_t *value, mcp4725_power_down_t *power_down);

/**
 * Set the power-down mode (fast write)
 * @param power_down Power-down mode
 * @return ESP_OK on success
 */
esp_err_t mcp4725_set_power_down(mcp4725_power_down_t power_down);

/**
 * Convert a voltage to a 12-bit DAC value
 * @param voltage Desired output voltage in volts
 * @param vref Reference voltage in volts (typically 3.3V or 5.0V)
 * @return 12-bit DAC value (0-4095), clamped to valid range
 */
uint16_t mcp4725_voltage_to_value(float voltage, float vref);

/**
 * Convert a 12-bit DAC value to voltage
 * @param value 12-bit DAC value (0-4095)
 * @param vref Reference voltage in volts (typically 3.3V or 5.0V)
 * @return Output voltage in volts
 */
float mcp4725_value_to_voltage(uint16_t value, float vref);

/**
 * Set the CV voltage range mode
 * This writes the appropriate voltage to the DAC (volatile), stores it in EEPROM (persistent),
 * and saves the mode setting to NVS (for restore on reboot).
 * @param range CV voltage range mode
 * @return ESP_OK on success
 */
esp_err_t mcp4725_set_cv_range(mcp4725_cv_range_t range);

/**
 * Get the current CV voltage range mode
 * @param range Pointer to store the current CV range mode
 * @return ESP_OK on success
 */
esp_err_t mcp4725_get_cv_range(mcp4725_cv_range_t *range);

#endif // _MCP4725_H
