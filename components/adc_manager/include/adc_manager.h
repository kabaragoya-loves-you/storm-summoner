#ifndef _ADC_MANAGER_H
#define _ADC_MANAGER_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

/**
 * ADC Manager Component
 * 
 * Provides centralized management of a single ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 * shared across multiple components. Handles unit initialization, channel registration,
 * and thread-safe reading operations.
 * 
 * Usage:
 * 1. Call adc_manager_init() once during system initialization
 * 2. Each component calls adc_manager_register_channel() for its channels
 * 3. Components call adc_manager_read() as needed
 * 4. Call adc_manager_deinit() during shutdown (optional)
 */

/**
 * Initialize the ADC manager for a specific ADC unit
 * 
 * Must be called before any other ADC manager functions.
 * Can only be called once - subsequent calls will return ESP_ERR_INVALID_STATE.
 * 
 * @param unit ADC unit to manage (ADC_UNIT_1 or ADC_UNIT_2)
 * @param bitwidth ADC resolution (typically ADC_BITWIDTH_12 for 12-bit)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t adc_manager_init(void);

/**
 * Register an ADC channel for use
 * 
 * Multiple components can register different channels on the same unit.
 * Channels must be registered before reading.
 * 
 * @param channel ADC channel to register (ADC_CHANNEL_0 through ADC_CHANNEL_N)
 * @param atten Attenuation setting for this channel (e.g., ADC_ATTEN_DB_12)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t adc_manager_register_channel(adc_channel_t channel, adc_atten_t atten);

/**
 * Read a raw ADC value from a registered channel
 * 
 * Thread-safe. Channel must have been registered via adc_manager_register_channel().
 * 
 * @param channel ADC channel to read
 * @param raw_value Pointer to store the raw ADC reading (0-4095 for 12-bit)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t adc_manager_read(adc_channel_t channel, int *raw_value);

/**
 * Read a calibrated ADC value in millivolts
 * 
 * Uses eFuse calibration data to return accurate voltage reading.
 * Thread-safe. Channel must have been registered via adc_manager_register_channel().
 * 
 * @param channel ADC channel to read
 * @param voltage_mv Pointer to store the calibrated voltage in millivolts
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t adc_manager_read_calibrated(adc_channel_t channel, int *voltage_mv);

/**
 * Check if ADC manager is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool adc_manager_is_initialized(void);

/**
 * Check whether a channel has been registered with the ADC manager.
 */
bool adc_manager_channel_is_registered(adc_channel_t channel);

/**
 * Deinitialize the ADC manager
 * 
 * Releases all ADC resources. After calling this, adc_manager_init() can be called again.
 */
void adc_manager_deinit(void);

#endif /* _ADC_MANAGER_H */

