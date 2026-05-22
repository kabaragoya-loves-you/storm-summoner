#ifndef SETTINGS_REGISTRY_H
#define SETTINGS_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Settings Registry Component
 * 
 * Maps user-facing setting IDs (e.g., "tempo.clock_standard") to component getter/setter calls.
 * Provides a semantic layer between USB CONFIG mode and the various component APIs.
 */

/**
 * Get a setting value by ID
 * 
 * @param id The setting ID (e.g., "tempo.clock_standard")
 * @param value Pointer to store the value (always uint32_t, cast as needed)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if ID unknown
 */
esp_err_t settings_registry_get_value(const char* id, uint32_t* value);

/**
 * Set a setting value by ID
 * 
 * @param id The setting ID (e.g., "tempo.clock_standard")
 * @param value The value to set (always uint32_t, cast as needed)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if ID unknown, or error from setter
 */
esp_err_t settings_registry_set_value(const char* id, uint32_t value);

/**
 * Get all setting values as JSON
 * 
 * Writes a JSON object with all settings: {"id1": value1, "id2": value2, ...}
 * 
 * @param buffer Buffer to write JSON to
 * @param buffer_size Size of buffer
 * @param written Pointer to store number of bytes written (can be NULL)
 * @return ESP_OK on success, ESP_ERR_NO_MEM if buffer too small
 */
esp_err_t settings_registry_get_all_values(char* buffer, size_t buffer_size, size_t* written);

/**
 * Check if a setting ID exists
 * 
 * @param id The setting ID
 * @return true if the setting exists, false otherwise
 */
bool settings_registry_exists(const char* id);

/**
 * Get the number of registered settings
 * 
 * @return Number of settings
 */
size_t settings_registry_count(void);

#endif // SETTINGS_REGISTRY_H
