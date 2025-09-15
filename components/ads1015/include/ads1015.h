#ifndef _ADS1015_H
#define _ADS1015_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// ADS1015 gain settings for programmable gain amplifier
typedef enum {
  ADS1015_GAIN_TWOTHIRDS = 0,  // +/- 6.144V range (default)
  ADS1015_GAIN_ONE = 1,        // +/- 4.096V range
  ADS1015_GAIN_TWO = 2,        // +/- 2.048V range
  ADS1015_GAIN_FOUR = 3,       // +/- 1.024V range
  ADS1015_GAIN_EIGHT = 4,      // +/- 0.512V range
  ADS1015_GAIN_SIXTEEN = 5     // +/- 0.256V range
} ads1015_gain_t;

// ADS1015 data rate settings
typedef enum {
  ADS1015_RATE_128SPS = 0,
  ADS1015_RATE_250SPS = 1,
  ADS1015_RATE_490SPS = 2,
  ADS1015_RATE_920SPS = 3,
  ADS1015_RATE_1600SPS = 4,   // Default
  ADS1015_RATE_2400SPS = 5,
  ADS1015_RATE_3300SPS = 6
} ads1015_rate_t;

/**
 * Initialize the ADS1015 ADC
 * @return ESP_OK on success
 */
esp_err_t ads1015_init(void);

/**
 * Check if ADS1015 is initialized
 * @return true if initialized, false otherwise
 */
bool ads1015_is_initialized(void);

/**
 * Read a single-ended channel
 * @param channel Channel to read (0-3)
 * @param gain Gain setting for this reading
 * @return 12-bit ADC value (0-4095), or -1 on error
 */
int16_t ads1015_read_channel(uint8_t channel, ads1015_gain_t gain);

/**
 * Read a single-ended channel with default gain (4.096V range)
 * @param channel Channel to read (0-3)
 * @return 12-bit ADC value (0-4095), or -1 on error
 */
int16_t ads1015_read_channel_default(uint8_t channel);

/**
 * Convert raw ADC value to voltage
 * @param raw_value Raw ADC value
 * @param gain Gain setting used for the reading
 * @return Voltage in volts
 */
float ads1015_raw_to_voltage(int16_t raw_value, ads1015_gain_t gain);

/**
 * Set the data rate for conversions
 * @param rate Data rate setting
 * @return ESP_OK on success
 */
esp_err_t ads1015_set_data_rate(ads1015_rate_t rate);

/**
 * Read two channels and return the ratio (channel/reference)
 * This is useful for ratiometric measurements that cancel out reference voltage drift
 * @param channel_num Channel to measure (0-3)
 * @param reference_channel Reference channel (0-3)
 * @param gain Gain setting for both readings
 * @return Ratio (0.0-1.0), or -1.0 on error
 */
float ads1015_read_ratiometric(uint8_t channel_num, uint8_t reference_channel, ads1015_gain_t gain);

#endif /* _ADS1015_H */
