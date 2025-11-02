#include "firmware_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "FIRMWARE_UPDATE"

// Firmware update state
static firmware_update_state_t s_firmware_state = FIRMWARE_UPDATE_IDLE;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_update_partition = NULL;
static size_t s_firmware_written = 0;
static size_t s_firmware_total = 0;

// Assets update state
static assets_update_state_t s_assets_state = ASSETS_UPDATE_IDLE;
static const esp_partition_t *s_assets_partition = NULL;
static size_t s_assets_written = 0;
static size_t s_assets_total = 0;

esp_err_t firmware_update_init(void) {
  ESP_LOGI(TAG, "Firmware update component initialized");
  return ESP_OK;
}

esp_err_t firmware_update_start(const uint8_t *data, size_t len) {
  if (s_firmware_state == FIRMWARE_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Firmware update already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting firmware OTA update (%u bytes)", (unsigned)len);

  // Get next OTA partition
  s_update_partition = esp_ota_get_next_update_partition(NULL);
  if (!s_update_partition) {
    ESP_LOGE(TAG, "No OTA partition found");
    s_firmware_state = FIRMWARE_UPDATE_ERROR;
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "Writing to partition: %s at offset 0x%lx",
    s_update_partition->label, (unsigned long)s_update_partition->address);

  // Begin OTA update
  esp_err_t err = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    s_firmware_state = FIRMWARE_UPDATE_ERROR;
    return err;
  }

  s_firmware_state = FIRMWARE_UPDATE_IN_PROGRESS;
  s_firmware_written = 0;
  s_firmware_total = len;

  return ESP_OK;
}

esp_err_t firmware_update_write(const uint8_t *data, size_t len) {
  if (s_firmware_state != FIRMWARE_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Firmware update not in progress");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_ota_write(s_ota_handle, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
    s_firmware_state = FIRMWARE_UPDATE_ERROR;
    return err;
  }

  s_firmware_written += len;
  ESP_LOGD(TAG, "Firmware write progress: %u/%u bytes",
    (unsigned)s_firmware_written, (unsigned)s_firmware_total);

  return ESP_OK;
}

esp_err_t firmware_update_finalize(void) {
  if (s_firmware_state != FIRMWARE_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Firmware update not in progress");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Finalizing firmware update...");

  // End OTA update
  esp_err_t err = esp_ota_end(s_ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    s_firmware_state = FIRMWARE_UPDATE_ERROR;
    return err;
  }

  // Set boot partition
  err = esp_ota_set_boot_partition(s_update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    s_firmware_state = FIRMWARE_UPDATE_ERROR;
    return err;
  }

  s_firmware_state = FIRMWARE_UPDATE_COMPLETE;
  ESP_LOGI(TAG, "Firmware update complete! Reboot to apply.");

  return ESP_OK;
}

firmware_update_state_t firmware_update_get_state(void) {
  return s_firmware_state;
}

uint8_t firmware_update_get_progress(void) {
  if (s_firmware_total == 0) return 0;
  return (uint8_t)((s_firmware_written * 100) / s_firmware_total);
}

esp_err_t assets_update_start(const uint8_t *data, size_t len) {
  if (s_assets_state == ASSETS_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Assets update already in progress");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Starting assets partition update (%u bytes)", (unsigned)len);

  // Find assets partition
  s_assets_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
    "assets"
  );

  if (!s_assets_partition) {
    ESP_LOGE(TAG, "Assets partition not found");
    s_assets_state = ASSETS_UPDATE_ERROR;
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "Found assets partition at offset 0x%lx, size %lu",
    (unsigned long)s_assets_partition->address,
    (unsigned long)s_assets_partition->size);

  // Erase partition
  esp_err_t err = esp_partition_erase_range(s_assets_partition, 0, s_assets_partition->size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to erase assets partition: %s", esp_err_to_name(err));
    s_assets_state = ASSETS_UPDATE_ERROR;
    return err;
  }

  s_assets_state = ASSETS_UPDATE_IN_PROGRESS;
  s_assets_written = 0;
  s_assets_total = len;

  return ESP_OK;
}

esp_err_t assets_update_write(const uint8_t *data, size_t len) {
  if (s_assets_state != ASSETS_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Assets update not in progress");
    return ESP_ERR_INVALID_STATE;
  }

  // Write to partition
  esp_err_t err = esp_partition_write(s_assets_partition, s_assets_written, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write to assets partition: %s", esp_err_to_name(err));
    s_assets_state = ASSETS_UPDATE_ERROR;
    return err;
  }

  s_assets_written += len;
  ESP_LOGD(TAG, "Assets write progress: %u/%u bytes",
    (unsigned)s_assets_written, (unsigned)s_assets_total);

  return ESP_OK;
}

esp_err_t assets_update_finalize(void) {
  if (s_assets_state != ASSETS_UPDATE_IN_PROGRESS) {
    ESP_LOGE(TAG, "Assets update not in progress");
    return ESP_ERR_INVALID_STATE;
  }

  s_assets_state = ASSETS_UPDATE_COMPLETE;
  ESP_LOGI(TAG, "Assets update complete! %u bytes written.", (unsigned)s_assets_written);

  return ESP_OK;
}

assets_update_state_t assets_update_get_state(void) {
  return s_assets_state;
}

uint8_t assets_update_get_progress(void) {
  if (s_assets_total == 0) return 0;
  return (uint8_t)((s_assets_written * 100) / s_assets_total);
}

esp_err_t firmware_update_process_file(const char *filename, const uint8_t *data, size_t len) {
  if (!filename || !data || len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Processing file: %s (%u bytes)", filename, (unsigned)len);

  // Check filename to determine update type
  if (strcmp(filename, "firmware.bin") == 0) {
    ESP_LOGI(TAG, "Detected firmware update file");
    
    esp_err_t err = firmware_update_start(data, len);
    if (err != ESP_OK) return err;
    
    err = firmware_update_write(data, len);
    if (err != ESP_OK) return err;
    
    return firmware_update_finalize();
  }
  else if (strcmp(filename, "assets.bin") == 0) {
    ESP_LOGI(TAG, "Detected assets update file");
    
    esp_err_t err = assets_update_start(data, len);
    if (err != ESP_OK) return err;
    
    err = assets_update_write(data, len);
    if (err != ESP_OK) return err;
    
    return assets_update_finalize();
  }
  else {
    ESP_LOGW(TAG, "Unknown file type: %s", filename);
    return ESP_ERR_NOT_SUPPORTED;
  }
}

