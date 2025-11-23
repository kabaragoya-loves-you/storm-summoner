#ifndef TOUCHWHEEL_ANALOG_H
#define TOUCHWHEEL_ANALOG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Callback type for analog position updates
// position: interpolated position (0.0 to 7.999)
// timestamp_ms: current timestamp
typedef void (*touchwheel_analog_position_cb_t)(float position, uint32_t timestamp_ms, void* user_data);

/**
 * Start analog sampling for touchwheel pads 0-7
 * @return ESP_OK on success
 */
esp_err_t touchwheel_analog_start(void);

/**
 * Stop analog sampling
 */
void touchwheel_analog_stop(void);

/**
 * Check if analog sampling is currently active
 * @return true if sampling is running
 */
bool touchwheel_analog_is_active(void);

/**
 * Get current interpolated position (0.0 to 7.999)
 * @param position_out Output parameter for position
 * @return ESP_OK if position is valid, ESP_ERR_INVALID_STATE if not sampling
 */
esp_err_t touchwheel_analog_get_position(float* position_out);

/**
 * Set callback for position updates (supports multiple callbacks)
 * @param callback Callback function (NULL to disable)
 * @param user_data User data passed to callback (used as unique identifier)
 */
void touchwheel_analog_set_position_callback(touchwheel_analog_position_cb_t callback, void* user_data);

/**
 * Remove callback for position updates
 * @param user_data User data that was used when registering callback
 */
void touchwheel_analog_remove_position_callback(void* user_data);

#endif // TOUCHWHEEL_ANALOG_H

