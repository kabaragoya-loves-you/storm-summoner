#ifndef _FIRMWARE_UPDATE_H
#define _FIRMWARE_UPDATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
  FIRMWARE_UPDATE_IDLE,
  FIRMWARE_UPDATE_IN_PROGRESS,
  FIRMWARE_UPDATE_COMPLETE,
  FIRMWARE_UPDATE_ERROR
} firmware_update_state_t;

typedef enum {
  ASSETS_UPDATE_IDLE,
  ASSETS_UPDATE_IN_PROGRESS,
  ASSETS_UPDATE_COMPLETE,
  ASSETS_UPDATE_ERROR
} assets_update_state_t;

/**
 * Initialize firmware update component
 * @return ESP_OK on success
 */
esp_err_t firmware_update_init(void);

/**
 * Start firmware OTA update
 * @param data Firmware binary data
 * @param len Length of firmware data
 * @return ESP_OK on success
 */
esp_err_t firmware_update_start(const uint8_t *data, size_t len);

/**
 * Write firmware data chunk
 * @param data Chunk data
 * @param len Chunk length
 * @return ESP_OK on success
 */
esp_err_t firmware_update_write(const uint8_t *data, size_t len);

/**
 * Finalize firmware update and set boot partition
 * @return ESP_OK on success
 */
esp_err_t firmware_update_finalize(void);

/**
 * Get current firmware update state
 * @return Current state
 */
firmware_update_state_t firmware_update_get_state(void);

/**
 * Get firmware update progress (0-100)
 * @return Progress percentage
 */
uint8_t firmware_update_get_progress(void);

/**
 * Start assets partition update
 * @param data Assets binary data (littlefs image)
 * @param len Length of assets data
 * @return ESP_OK on success
 */
esp_err_t assets_update_start(const uint8_t *data, size_t len);

/**
 * Write assets data chunk
 * @param data Chunk data
 * @param len Chunk length
 * @return ESP_OK on success
 */
esp_err_t assets_update_write(const uint8_t *data, size_t len);

/**
 * Finalize assets update
 * @return ESP_OK on success
 */
esp_err_t assets_update_finalize(void);

/**
 * Get current assets update state
 * @return Current state
 */
assets_update_state_t assets_update_get_state(void);

/**
 * Get assets update progress (0-100)
 * @return Progress percentage
 */
uint8_t assets_update_get_progress(void);

/**
 * Process file from MSC drive
 * @param filename Name of file
 * @param data File data
 * @param len File length
 * @return ESP_OK on success
 */
esp_err_t firmware_update_process_file(const char *filename, const uint8_t *data, size_t len);

#endif /* _FIRMWARE_UPDATE_H */

