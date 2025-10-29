#ifndef ASSETS_MANAGER_H
#define ASSETS_MANAGER_H

#include "assets_types.h"
#include "esp_err.h"

/**
 * Initialize the assets manager
 * - Mounts the LittleFS partition
 * - Loads and parses manifest.json
 * Returns ESP_OK on success
 */
esp_err_t assets_manager_init(void);

/**
 * Get the number of devices in the manifest
 */
uint32_t assets_get_device_count(void);

/**
 * Get device info by index
 * Returns ESP_OK on success, ESP_ERR_INVALID_ARG if index out of range
 */
esp_err_t assets_get_device_info(uint32_t idx, const char **slug, const char **name, const char **vendor);

/**
 * Load a device definition by slug
 * Tries to load from binary cache first, falls back to JSON parsing
 * Returns pointer to device_def_t on success, NULL on failure
 * Device is allocated in PSRAM
 */
device_def_t *assets_load_device(const char *slug);

/**
 * Free a device definition
 * Releases all PSRAM allocations
 */
void assets_free_device(device_def_t *device);

/**
 * Get control by CC number
 * Returns pointer to control or NULL if not found
 */
const midi_control_t *assets_get_control_by_cc(const device_def_t *device, uint8_t cc_num);

/**
 * Get control by array index
 * Returns pointer to control or NULL if index out of range
 */
const midi_control_t *assets_get_control_by_index(const device_def_t *device, uint16_t idx);

/**
 * Get program change info for device
 * Returns pointer to PC info or NULL if device doesn't support PC
 */
const program_change_info_t *assets_get_pc_info(const device_def_t *device);

#endif // ASSETS_MANAGER_H

