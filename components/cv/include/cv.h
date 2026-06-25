#ifndef _CV_H
#define _CV_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "input_mode.h"

/**
 * Initialize the CV component
 * @param enable_logging Enable periodic value logging
 * @return ESP_OK on success
 */
esp_err_t cv_init(bool enable_logging);

/**
 * Enable CV sampling
 */
void cv_enable(void);

/**
 * Disable CV sampling
 */
void cv_disable(void);

/**
 * Check if CV cable is connected using ADC-based detection
 * @return true if cable is connected, false otherwise
 */
bool cv_is_cable_connected(void);

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
 * Hardware switch channel for a CV voltage range (front-end routing).
 */
uint8_t cv_get_switch_channel_for_range(cv_range_t range);

/**
 * Set the CV processing mode (LINEAR or PITCH)
 * @param mode The mode to use
 */
void cv_set_mode(cv_mode_t mode);

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

/**
 * Auto-calibrate a CV range by tracking min/max over a period
 * User should swing the input from lowest to highest voltage during this time.
 * Applies 1% margin on each extreme for headroom.
 * @param range The voltage range to calibrate
 * @param duration_ms Duration in milliseconds to sample (e.g., 5000 for 5 seconds)
 * @return ESP_OK on success
 */
esp_err_t cv_auto_calibrate(cv_range_t range, uint32_t duration_ms);

/**
 * Calibrate cable detection by capturing disconnected signatures for all ranges
 * User must remove any cable from the CV jack before calling this function.
 * The function cycles through all 5 voltage ranges and captures the switch pin
 * voltage when no cable is connected, storing these signatures in NVS.
 * @return ESP_OK on success
 */
esp_err_t cv_calibrate_cable_detect(void);

/**
 * Get the disconnected signature value for a specific range
 * @param range The voltage range to query
 * @param signature Pointer to store the signature value (mV)
 */
void cv_get_disc_signature(cv_range_t range, int16_t *signature);

/**
 * Set the CV pitch standard (for CV_MODE_PITCH)
 * @param standard The pitch standard to use
 */
void cv_set_pitch_standard(cv_pitch_standard_t standard);

/**
 * Get the current CV pitch standard
 * @return Current pitch standard
 */
cv_pitch_standard_t cv_get_pitch_standard(void);

/**
 * Get the current pitch as a MIDI note number (for CV_MODE_PITCH)
 * @return MIDI note number (0-127)
 */
uint8_t cv_get_pitch_note(void);

/**
 * Read pitch note directly from ADC (bypasses caching/filtering)
 * Use this for time-critical pitch capture like gate triggering
 * @return MIDI note number (0-127)
 */
uint8_t cv_read_pitch_note_now(void);

/**
 * Enable audio envelope follower mode
 * @param config Audio configuration (copied internally)
 */
void cv_enable_audio_mode(const audio_config_t* config);

/**
 * Disable audio envelope follower mode
 */
void cv_disable_audio_mode(void);

/**
 * Check if audio mode is currently active
 * @return true if audio envelope follower is running
 */
bool cv_is_audio_mode_active(void);

/**
 * Update audio configuration while running
 * @param config New audio configuration
 */
void cv_update_audio_config(const audio_config_t* config);

/**
 * Get the current envelope follower value
 * @return Envelope value (0.0 - 1.0)
 */
float cv_get_envelope_value(void);

/**
 * Calibrate audio sensitivity by measuring peak amplitude
 * @param duration_ms How long to sample (user should play loudest audio)
 * @return Recommended sensitivity value (0-255)
 */
uint8_t cv_audio_calibrate(uint32_t duration_ms);

/**
 * Read raw ADC value immediately (for calibration UI)
 * @return Raw ADC value
 */
int16_t cv_read_raw_now(void);

#endif /* _CV_H */
