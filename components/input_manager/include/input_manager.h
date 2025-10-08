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
 * Notify input manager of cable connection change
 * @param connected true if cable connected, false if disconnected
 */
void input_manager_cable_changed(bool connected);

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

#endif /* _INPUT_MANAGER_H */
