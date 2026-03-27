#include "version.h"
#include "version_build.h"  // Auto-generated at build time
#include "app_settings.h"
#include "esp_mac.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char* TAG = "VERSION";

// Compile-time version from CMake definitions (fallbacks if not defined)
#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR 0
#endif

#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR 0
#endif

#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

// Assets checksum is stored in NVS only - not compiled into firmware
// This keeps firmware and assets truly decoupled
#define ASSETS_CHECKSUM_UNKNOWN "unknown"

// NVS key for assets checksum
#define NVS_KEY_ASSETS_CHECKSUM "assets_csum"

// Static buffers for generated strings
static char s_serial[13] = {0};           // 12 hex chars + null
static char s_version_string[64] = {0};   // "X.Y.Z (hash)"
static char s_assets_checksum[9] = {0};   // 8 hex chars + null
static bool s_initialized = false;

static version_info_t s_version_info = {
  .major = FW_VERSION_MAJOR,
  .minor = FW_VERSION_MINOR,
  .build = FW_BUILD_NUMBER,
  .git_hash = FW_GIT_HASH,
  .serial = s_serial,
  .assets_checksum = s_assets_checksum
};

esp_err_t version_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Version already initialized");
    return ESP_OK;
  }

  // Get MAC address from eFuse (factory-burned, unique per chip)
  uint8_t mac[6];
  esp_err_t ret = esp_efuse_mac_get_default(mac);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
    strcpy(s_serial, "FFFFFFFFFFFF");  // 12 chars + null fits in 13-byte buffer
  } else {
    // Format MAC as hex serial number
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  // Load assets checksum from NVS (only set after WebUSB assets update)
  ret = app_settings_load_str(NVS_KEY_ASSETS_CHECKSUM, s_assets_checksum,
    sizeof(s_assets_checksum));
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Loaded assets checksum from NVS: %s", s_assets_checksum);
  } else {
    ESP_LOGW(TAG, "No assets checksum in NVS (ret=%s), using default",
      esp_err_to_name(ret));
    strncpy(s_assets_checksum, ASSETS_CHECKSUM_UNKNOWN, sizeof(s_assets_checksum) - 1);
    s_assets_checksum[sizeof(s_assets_checksum) - 1] = '\0';
  }

  // Build full version string
  snprintf(s_version_string, sizeof(s_version_string), "%u.%u.%lu (%s)",
           (unsigned)s_version_info.major,
           (unsigned)s_version_info.minor,
           (unsigned long)s_version_info.build,
           s_version_info.git_hash);

  s_initialized = true;

  ESP_LOGI(TAG, "Firmware: %s", s_version_string);
  ESP_LOGI(TAG, "Serial: %s", s_serial);
  ESP_LOGI(TAG, "Assets: %s", s_version_info.assets_checksum);

  return ESP_OK;
}

uint8_t version_get_major(void) {
  return s_version_info.major;
}

uint8_t version_get_minor(void) {
  return s_version_info.minor;
}

uint32_t version_get_build(void) {
  return s_version_info.build;
}

const char* version_get_git_hash(void) {
  return s_version_info.git_hash;
}

const char* version_get_assets_checksum(void) {
  return s_version_info.assets_checksum;
}

esp_err_t version_set_assets_checksum(const char* checksum) {
  if (!checksum || strlen(checksum) != 8) {
    ESP_LOGE(TAG, "Invalid assets checksum: must be 8 characters");
    return ESP_ERR_INVALID_ARG;
  }

  strncpy(s_assets_checksum, checksum, sizeof(s_assets_checksum) - 1);
  s_assets_checksum[sizeof(s_assets_checksum) - 1] = '\0';

  esp_err_t ret = app_settings_save_str(NVS_KEY_ASSETS_CHECKSUM, s_assets_checksum);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save assets checksum to NVS: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Assets checksum updated: %s", s_assets_checksum);
  return ESP_OK;
}

const char* version_get_serial(void) {
  if (!s_initialized) {
    ESP_LOGW(TAG, "Version not initialized, serial unavailable");
    return "FFFFFFFFFFFF";
  }
  return s_serial;
}

const char* version_get_string(void) {
  if (!s_initialized) {
    // Return static compile-time version if not initialized
    return FW_GIT_HASH;
  }
  return s_version_string;
}

const version_info_t* version_get_info(void) {
  return &s_version_info;
}

void version_print(void) {
  ESP_LOGI(TAG, "====== FIRMWARE VERSION ======");
  ESP_LOGI(TAG, "Version: %u.%u.%lu",
           (unsigned)s_version_info.major,
           (unsigned)s_version_info.minor,
           (unsigned long)s_version_info.build);
  ESP_LOGI(TAG, "Git Hash: %s", s_version_info.git_hash);
  ESP_LOGI(TAG, "Assets: %s", s_version_info.assets_checksum);
  ESP_LOGI(TAG, "Serial: %s", s_serial);
  ESP_LOGI(TAG, "Full: %s", s_version_string);
  ESP_LOGI(TAG, "==============================");
}

