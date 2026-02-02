#ifndef _DAC_H
#define _DAC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// CV output voltage range modes
// Maps to cv_range_t values for consistency
typedef enum {
  MCP4725_RANGE_BIPOLAR_10V = 0,   // ±10V range, 1.416V DAC output (switch ch 0)
  MCP4725_RANGE_10V = 1,           // 0-10V range, 2.481V DAC output (switch ch 1)
  MCP4725_RANGE_BIPOLAR_5V = 2,    // ±5V range, 1.241V DAC output (switch ch 1)
  MCP4725_RANGE_5V = 3,            // 0-5V range, 1.983V DAC output (switch ch 2)
  MCP4725_RANGE_3V3 = 4            // 0-3.3V range, 1.650V DAC output (switch ch 3)
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
esp_err_t dac_init(void);

/**
 * Check if MCP4725 is initialized
 * @return true if initialized, false otherwise
 */
bool dac_is_initialized(void);

/**
 * Set the DAC output value (fast write, volatile)
 * This writes only to the DAC register. Value is lost on power-off.
 * @param value 12-bit DAC value (0-4095)
 * @return ESP_OK on success
 */
esp_err_t dac_set_value(uint16_t value);

/**
 * Set the DAC output value and save to EEPROM (persistent)
 * This writes to both the DAC register and EEPROM. Value persists after power-off.
 * Note: EEPROM has limited write cycles (~100k), so use sparingly.
 * @param value 12-bit DAC value (0-4095)
 * @param power_down Power-down mode (typically MCP4725_PD_NORMAL)
 * @return ESP_OK on success
 */
esp_err_t dac_set_value_eeprom(uint16_t value, mcp4725_power_down_t power_down);

/**
 * Get the current DAC output value from the DAC register
 * @param value Pointer to store the current 12-bit DAC value
 * @return ESP_OK on success
 */
esp_err_t dac_get_value(uint16_t *value);

/**
 * Get the value stored in EEPROM
 * @param value Pointer to store the 12-bit EEPROM value
 * @param power_down Pointer to store the power-down mode (can be NULL if not needed)
 * @return ESP_OK on success
 */
esp_err_t dac_get_eeprom_value(uint16_t *value, mcp4725_power_down_t *power_down);

/**
 * Read and log current DAC register and EEPROM values for debugging
 * @return ESP_OK on success
 */
esp_err_t dac_debug_readback(void);

/**
 * Calibrate DAC VREF by measuring actual VCC via ADC reference channel
 * Uses the same reference channel as expression pedal (GPIO18/ADC1_CH2)
 * @return ESP_OK on success
 */
esp_err_t dac_calibrate_vref(void);

/**
 * Get the current calibrated VREF value
 * @return Current VREF in volts
 */
float dac_get_vref(void);

/**
 * Schedule VREF calibration after a delay
 * Use this to defer calibration until after boot has fully settled
 * @param delay_ms Delay in milliseconds before calibration runs
 * @return ESP_OK on success
 */
esp_err_t dac_schedule_calibration(uint32_t delay_ms);

/**
 * Set the power-down mode (fast write)
 * @param power_down Power-down mode
 * @return ESP_OK on success
 */
esp_err_t dac_set_power_down(mcp4725_power_down_t power_down);

/**
 * Convert a voltage to a 12-bit DAC value
 * @param voltage Desired output voltage in volts
 * @param vref Reference voltage in volts (typically 3.3V or 5.0V)
 * @return 12-bit DAC value (0-4095), clamped to valid range
 */
uint16_t dac_voltage_to_value(float voltage, float vref);

/**
 * Convert a 12-bit DAC value to voltage
 * @param value 12-bit DAC value (0-4095)
 * @param vref Reference voltage in volts (typically 3.3V or 5.0V)
 * @return Output voltage in volts
 */
float dac_value_to_voltage(uint16_t value, float vref);

/**
 * Set the CV voltage range mode
 * This writes the appropriate voltage to the DAC (volatile), stores it in EEPROM (persistent),
 * and saves the mode setting to NVS (for restore on reboot).
 * @param range CV voltage range mode
 * @return ESP_OK on success
 */
esp_err_t dac_set_cv_range(mcp4725_cv_range_t range);

/**
 * Get the current CV voltage range mode
 * @param range Pointer to store the current CV range mode
 * @return ESP_OK on success
 */
esp_err_t dac_get_cv_range(mcp4725_cv_range_t *range);

#endif // _MCP4725_H
