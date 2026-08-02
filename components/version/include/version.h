#ifndef VERSION_H
#define VERSION_H

#include <stdint.h>
#include "esp_err.h"

/**
 * Version information structure
 */
typedef struct {
  uint8_t major;              // Major version number
  uint8_t minor;              // Minor version number
  uint32_t build;             // Build number
  const char* git_hash;       // Git commit hash (short)
  const char* serial;         // Unique device serial (derived from MAC)
  const char* assets_checksum; // Assets manifest checksum (8 chars)
} version_info_t;

/**
 * Initialize the version component
 * Reads eFuse MAC address and prepares serial number
 * @return ESP_OK on success
 */
esp_err_t version_init(void);

/**
 * Get major firmware version
 * @return Major version number
 */
uint8_t version_get_major(void);

/**
 * Get minor firmware version
 * @return Minor version number
 */
uint8_t version_get_minor(void);

/**
 * Get build number
 * @return Build number
 */
uint32_t version_get_build(void);

/**
 * Get git hash string
 * @return Git commit hash (e.g., "abc1234" or "abc1234-dirty")
 */
const char* version_get_git_hash(void);

/**
 * Get assets checksum string
 * @return Assets manifest checksum (8 chars, e.g., "2e9a1904")
 */
const char* version_get_assets_checksum(void);

/**
 * Set assets checksum (called after successful assets update)
 * Saves to NVS for persistence across reboots
 * @param checksum 8-character hex checksum string
 * @return ESP_OK on success
 */
esp_err_t version_set_assets_checksum(const char* checksum);

/**
 * If NVS has no assets checksum yet, SHA256 the live assets partition
 * (same identity as promote_release / releases.json) and persist the first
 * 8 hex chars. Call only after the assets LittleFS mount succeeds so an
 * empty partition between `idf.py flash` and `assets-flash` is not stamped.
 * No-op when a real checksum is already loaded or set.
 * @return ESP_OK on success or when already known
 */
esp_err_t version_reconcile_assets_checksum(void);

/**
 * Get unique device serial number
 * Derived from ESP32's factory-burned eFuse MAC address
 * @return Serial number string (e.g., "001122334455")
 */
const char* version_get_serial(void);

/**
 * Get full version string
 * @return Version string (e.g., "0.1.42 (abc1234)")
 */
const char* version_get_string(void);

/**
 * Get full version info struct
 * @return Pointer to version info (read-only)
 */
const version_info_t* version_get_info(void);

/**
 * Print version info to log
 */
void version_print(void);

#endif // VERSION_H

