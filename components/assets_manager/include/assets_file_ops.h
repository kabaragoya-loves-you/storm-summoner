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
 * Scans /assets/scenes/ for scene_*.json files and rebuilds manifest.json
 * 
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_scenes_manifest(void);

/**
 * @brief Regenerate the devices manifest
 * 
 * Scans /assets/devices/ for device JSON files and rebuilds manifest.json
 * 
 * @return ESP_OK on success
 */
esp_err_t assets_regenerate_devices_manifest(void);

/**
 * @brief Regenerate the images manifest
 * 
 * Scans /assets/images/ for *.bin and *.bin.z files and rebuilds manifest.json
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

