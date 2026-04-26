#ifndef BUMP_H
#define BUMP_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize the bump sensor (LIS3DHTR accelerometer)
 * Configures I2C device, registers, GPIO interrupt, and starts detection task
 * 
 * @param enable_logging Enable verbose logging of bump events
 */
void bump_init(bool enable_logging);

/**
 * Get the hardware click detection threshold
 * This controls the sensitivity of the hardware tap detection circuit
 * 
 * @return Current threshold value (0-127)
 */
uint8_t bump_get_threshold(void);

/**
 * Set the hardware click detection threshold
 * This controls the sensitivity of the hardware tap detection circuit
 * Higher values = less sensitive (require harder taps)
 * 
 * @param threshold Threshold value (0-127)
 */
void bump_set_threshold(uint8_t threshold);

/**
 * Get the debounce delay in milliseconds
 * This is the minimum time between bump events
 * 
 * @return Debounce delay in milliseconds
 */
uint32_t bump_get_debounce(void);

/**
 * Set the debounce delay in milliseconds
 * This is the minimum time between bump events
 * 
 * @param ms Debounce delay in milliseconds
 */
void bump_set_debounce(uint32_t ms);

/**
 * Get the intensity threshold in milligravity (mg)
 * Only bumps with acceleration magnitude above this threshold generate events
 * 
 * @return Intensity threshold in mg
 */
uint32_t bump_get_intensity_threshold(void);

/**
 * Set the intensity threshold in milligravity (mg)
 * Only bumps with acceleration magnitude above this threshold generate events
 * This allows filtering out gentle taps while responding to harder bumps
 * 
 * @param threshold_mg Intensity threshold in mg (e.g., 500 = 0.5g)
 */
void bump_set_intensity_threshold(uint32_t threshold_mg);

/**
 * Get the current sensitivity level (1-10 scale)
 * 
 * @return Sensitivity level (1 = very sensitive, 10 = very insensitive)
 */
uint8_t bump_get_sensitivity_level(void);

/**
 * Set the sensitivity level (1-10 scale)
 * This is a convenient way to set both hardware and software thresholds
 * in a coordinated manner using pre-calibrated presets
 * 
 * Level 1 = very sensitive (light taps)
 * Level 5 = medium sensitivity (default)
 * Level 10 = very insensitive (hard bumps only)
 * 
 * @param level Sensitivity level (1-10)
 */
void bump_set_sensitivity_level(uint8_t level);

#endif // BUMP_H
