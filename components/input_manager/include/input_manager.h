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

// Re-export the input mode functions for convenience
esp_err_t input_set_mode(input_mode_t mode);
input_mode_t input_get_mode(void);
bool input_is_mode_active(input_mode_t mode);

#endif /* _INPUT_MANAGER_H */
