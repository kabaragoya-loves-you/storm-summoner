#ifndef ASSETS_FILE_OPS_H
#define ASSETS_FILE_OPS_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Called when a file is created or modified
 * 
 * Triggers manifest regeneration if the file is in a managed folder
 * (scenes, devices, or images).
 * 
 * @param path Full path to the file
 */
void assets_file_created(const char *path);

/**
 * @brief Called when a file is deleted
 * 
 * Triggers manifest regeneration if the file was in a managed folder.
 * 
 * @param path Full path to the deleted file
 */
void assets_file_deleted(const char *path);

/**
 * @brief Regenerate the scenes manifest
 *
 * Scans /userdata/scenes/ (Phase 2: scenes moved off the RO partition) for
 * numbered scene JSON files and rebuilds /userdata/scenes/manifest.json.
 *
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_scenes_manifest(void);

/**
 * @brief Regenerate the SHARED (RO) devices manifest
 *
 * Scans /assets/devices/ for device JSON files and rebuilds the shared
 * manifest. Only invoked from the dev console / build pipeline; the released
 * build ships a pre-generated manifest. Runtime CDC writes under /assets are
 * rejected by the Phase 4 gate.
 *
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_devices_manifest(void);

/**
 * @brief Regenerate the USER (RW) devices manifest
 *
 * Scans /userdata/devices/ for device JSON files and rebuilds
 * /userdata/devices/manifest.json. Triggered automatically by
 * assets_file_created/deleted whenever a file lands under /userdata/devices/.
 *
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_user_devices_manifest(void);

/**
 * @brief Reject PUT when a new user pedal file would claim an existing manifest slug.
 *
 * Updates to an existing path are allowed. New .json files under devices/user/
 * are blocked if user.<basename>@0 is already registered (prevents accidental overwrite).
 *
 * @param full_path Resolved filesystem path (e.g. /userdata/devices/user/foo.json)
 * @return ESP_OK if PUT may proceed, ESP_ERR_INVALID_STATE if slug is taken
 */
esp_err_t assets_validate_user_pedal_put(const char *full_path);

/**
 * @brief Regenerate the images manifest
 * 
 * Scans /assets/images/ for .bin and .bin.z files and rebuilds manifest.json
 * 
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_images_manifest(void);

/**
 * @brief Check if a path is within the assets directory
 * 
 * @param path Path to check
 * @return true if path is under /assets/
 */
bool assets_is_valid_path(const char *path);

/**
 * @brief Get the folder type for a path
 * 
 * @param path Full path to check
 * @return "scenes", "devices", "images", or NULL if not a managed folder
 */
const char *assets_get_folder_type(const char *path);

/**
 * @brief Recursively delete a directory and all its contents
 * 
 * @param path Full path to directory to delete
 * @return ESP_OK on success
 */
esp_err_t assets_recursive_delete(const char *path);

/**
 * @brief Extract a ZIP archive from memory to filesystem
 * 
 * @param zip_data Pointer to ZIP data in memory (PSRAM recommended)
 * @param zip_size Size of ZIP data in bytes
 * @param dest_path Destination directory path
 * @return ESP_OK on success
 */
esp_err_t assets_extract_zip(const uint8_t *zip_data, size_t zip_size, const char *dest_path);

#endif // ASSETS_FILE_OPS_H

