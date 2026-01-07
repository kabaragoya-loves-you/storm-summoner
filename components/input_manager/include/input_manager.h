#ifndef _INPUT_MANAGER_H
#define _INPUT_MANAGER_H

#include "input_mode.h"
#include "esp_err.h"

/**
 * Initialize the input manager
 * @return ESP_OK on success
 */
esp_err_t input_manager_init(void);

/**
 * Notify input manager of CV cable connection change
 * @param connected true if cable connected, false if disconnected
 */
void input_manager_cable_changed(bool connected);

/**
 * Notify input manager of Expression cable connection change
 * @param connected true if cable connected, false if disconnected
 */
void input_manager_expression_cable_changed(bool connected);

/**
 * Enable or disable cable detection for CV/clock sync processing
 * When disabled, CV and clock sync will process signals regardless of cable state
 * @param enable true to enable cable detection (default), false to ignore cable status
 */
void input_set_cable_detection_enabled(bool enable);

/**
 * Get current cable detection enabled state
 * @return true if cable detection is enabled, false if disabled
 */
bool input_get_cable_detection_enabled(void);

// Re-export the input mode functions for convenience
esp_err_t input_set_mode(input_mode_t mode);
input_mode_t input_get_mode(void);
bool input_is_mode_active(input_mode_t mode);

/**
 * Set velocity mode for NOTE mode
 * @param mode Velocity mode to use
 */
void input_set_velocity_mode(velocity_mode_t mode);

/**
 * Get current velocity mode
 * @return Current velocity mode
 */
velocity_mode_t input_get_velocity_mode(void);

/**
 * Set fixed velocity value (for VELOCITY_MODE_FIXED)
 * @param velocity Velocity value (1-127)
 */
void input_set_fixed_velocity(uint8_t velocity);

/**
 * Get fixed velocity value
 * @return Fixed velocity value
 */
uint8_t input_get_fixed_velocity(void);

/**
 * Send NOTE OFF for any active CV/Gate note and reset state
 * Call this when entering programming mode to prevent stuck notes
 */
void input_manager_release_active_notes(void);

#endif /* _INPUT_MANAGER_H */
