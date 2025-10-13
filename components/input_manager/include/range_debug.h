#ifndef RANGE_DEBUG_H
#define RANGE_DEBUG_H

#include "esp_err.h"

/**
 * @brief Initialize CV range selection debug buttons
 * 
 * Configures GPIO pins 11-15 as tactile buttons for testing CV range switching.
 * Each button triggers a different CV range:
 * - GPIO11: Bipolar ±10V
 * - GPIO12: Unipolar 0-10V
 * - GPIO13: Bipolar ±5V
 * - GPIO14: Unipolar 0-5V
 * - GPIO15: Unipolar 0-3.3V
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t range_debug_init(void);

#endif // RANGE_DEBUG_H

