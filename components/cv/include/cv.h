#ifndef _CV_H
#define _CV_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "input_mode.h"

/**
 * Initialize the CV component
 * @return ESP_OK on success
 */
esp_err_t cv_init(void);

/**
 * Enable CV sampling
 */
void cv_enable(void);

/**
 * Disable CV sampling
 */
void cv_disable(void);

/**
 * Get the current CV value (filtered ADC reading)
 * @return Filtered ADC value
 */
float cv_get_value(void);

/**
 * Get the current CV value as MIDI (0-127)
 * @return MIDI value based on current mode
 */
uint8_t cv_get_midi_value(void);

/**
 * Set the CV input mode
 * @param mode The CV mode to use
 */
void cv_set_mode(cv_mode_t mode);

/**
 * Get the current CV input mode
 * @return Current CV mode
 */
cv_mode_t cv_get_mode(void);

/**
 * Calibrate the CV input
 * @param offset Offset to add to raw readings
 * @param scale Scale factor to apply
 */
void cv_calibrate(float offset, float scale);

/**
 * Set the CV voltage range (which PCA9536 channel to use)
 * @param range The voltage range to use
 */
void cv_set_range(cv_range_t range);

/**
 * Get the current CV voltage range
 * @return Current voltage range
 */
cv_range_t cv_get_range(void);

/**
 * Set the CV deadzone (MIDI value change threshold)
 * @param deadzone The deadzone value (0-127)
 */
void cv_set_deadzone(uint8_t deadzone);

/**
 * Get the current CV deadzone
 * @return Current deadzone value
 */
uint8_t cv_get_deadzone(void);

/**
 * Set min/max calibration values for a specific range
 * @param range The voltage range to calibrate
 * @param min_value The ADC value that maps to MIDI 0
 * @param max_value The ADC value that maps to MIDI 127
 */
void cv_set_calibration(cv_range_t range, int16_t min_value, int16_t max_value);

/**
 * Get min/max calibration values for a specific range
 * @param range The voltage range to query
 * @param min_value Pointer to store min value
 * @param max_value Pointer to store max value
 */
void cv_get_calibration(cv_range_t range, int16_t *min_value, int16_t *max_value);

#endif /* _CV_H */
