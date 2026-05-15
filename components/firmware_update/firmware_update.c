#include "firmware_update.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "version.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define TAG "FIRMWARE_UPDATE"

// Partition table on flash. ESP32-P4 default offset is 0x8000; the sector is
// 4 KB and the table data area is 0xC00 bytes plus a 0x20-byte MD5 footer.
#define PT_FLASH_OFFSET   0x8000
#define PT_SECTOR_SIZE    0x1000
#define PT_DATA_MAX       0xC00
// Region reserved for bootloader (0x0000-0x8000) + partition table itself
// (0x8000-0x9000). The first user partition entry conventionally starts at
// 0x9000 (NVS in the default IDF layout). Anything below this overlaps either
// the bootloader image or the PT it's trying to replace, both of which would
// brick the device on next boot.
#define RESERVED_REGION_END 0x9000

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

  // Find the `assets` partition. Since the partition split (v(N+2)), this is
  // STRICTLY the read-only shared content (UI images + shared MIDI device DB).
  // User data (scenes, user-defined / cloned pedals, parsed-device cache)
  // lives on the separate `userdata` partition and is NEVER touched by an
  // assets OTA. This is the whole point of the split.
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

// ============================================================================
// Partition table OTA (Phase 0 / v(N+1))
// ============================================================================

static partition_table_update_state_t s_pt_state = PT_UPDATE_IDLE;
static uint8_t *s_pt_staging = NULL;
static size_t s_pt_staging_len = 0;
static size_t s_pt_received = 0;

static void pt_release_staging(void) {
  if (s_pt_staging) {
    heap_caps_free(s_pt_staging);
    s_pt_staging = NULL;
  }
  s_pt_staging_len = 0;
  s_pt_received = 0;
}

esp_err_t partition_table_update_start(size_t total_len) {
  if (total_len == 0 || total_len > PT_SECTOR_SIZE) {
    ESP_LOGE(TAG, "PT update: invalid total_len %u (must be 1..%u)",
      (unsigned)total_len, (unsigned)PT_SECTOR_SIZE);
    return ESP_ERR_INVALID_ARG;
  }
  // Discard any prior session.
  pt_release_staging();
  s_pt_staging = heap_caps_malloc(total_len, MALLOC_CAP_SPIRAM);
  if (!s_pt_staging) {
    ESP_LOGE(TAG, "PT update: failed to allocate %u bytes in PSRAM",
      (unsigned)total_len);
    s_pt_state = PT_UPDATE_ERROR;
    return ESP_ERR_NO_MEM;
  }
  s_pt_staging_len = total_len;
  s_pt_received = 0;
  s_pt_state = PT_UPDATE_RECEIVING;
  ESP_LOGI(TAG, "PT update: started (%u bytes)", (unsigned)total_len);
  return ESP_OK;
}

esp_err_t partition_table_update_write(const uint8_t *data, size_t len) {
  if (s_pt_state != PT_UPDATE_RECEIVING || !s_pt_staging) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_pt_received + len > s_pt_staging_len) {
    ESP_LOGE(TAG, "PT update: write would overflow buffer (%u + %u > %u)",
      (unsigned)s_pt_received, (unsigned)len, (unsigned)s_pt_staging_len);
    s_pt_state = PT_UPDATE_ERROR;
    return ESP_ERR_INVALID_SIZE;
  }
  memcpy(s_pt_staging + s_pt_received, data, len);
  s_pt_received += len;
  return ESP_OK;
}

esp_err_t partition_table_update_verify(char *err_msg, size_t err_msg_size) {
  if (err_msg && err_msg_size) err_msg[0] = '\0';

  if (s_pt_state != PT_UPDATE_RECEIVING || !s_pt_staging) {
    if (err_msg) snprintf(err_msg, err_msg_size, "no upload in progress");
    return ESP_ERR_INVALID_STATE;
  }
  if (s_pt_received != s_pt_staging_len) {
    if (err_msg) snprintf(err_msg, err_msg_size,
      "incomplete: received %u of %u bytes",
      (unsigned)s_pt_received, (unsigned)s_pt_staging_len);
    return ESP_ERR_INVALID_SIZE;
  }

  // esp_partition_table_verify expects a pointer to an array of esp_partition_info_t
  // entries. The MD5 footer is checked when an entry with the magic 0xEBEB is
  // encountered. log_errors=true makes IDF print diagnostics to the console.
  int num_partitions = 0;
  esp_err_t err = esp_partition_table_verify(
    (const esp_partition_info_t *)s_pt_staging, true, &num_partitions);
  if (err != ESP_OK) {
    if (err_msg) snprintf(err_msg, err_msg_size,
      "esp_partition_table_verify failed: %s", esp_err_to_name(err));
    ESP_LOGE(TAG, "PT update: verify failed: %s", esp_err_to_name(err));
    return err;
  }

  // Additional sanity: every partition entry must lie within the chip's flash
  // and not overlap the bootloader region. esp_partition_table_verify does the
  // chip-size check but not the bootloader-overlap check.
  uint32_t flash_size = 0;
  if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
    flash_size = 0; // Treat as unknown; skip the chip-size cross-check.
  }
  const esp_partition_info_t *entries = (const esp_partition_info_t *)s_pt_staging;
  for (int i = 0; i < num_partitions; i++) {
    uint32_t off = entries[i].pos.offset;
    uint32_t sz = entries[i].pos.size;
    if (off < RESERVED_REGION_END) {
      if (err_msg) snprintf(err_msg, err_msg_size,
        "entry %d (offset 0x%lx) overlaps bootloader/PT region (< 0x%lx)",
        i, (unsigned long)off, (unsigned long)RESERVED_REGION_END);
      ESP_LOGE(TAG, "PT update: %s", err_msg ? err_msg : "reserved overlap");
      return ESP_ERR_INVALID_ARG;
    }
    if (flash_size > 0 && (uint64_t)off + (uint64_t)sz > (uint64_t)flash_size) {
      if (err_msg) snprintf(err_msg, err_msg_size,
        "entry %d (offset 0x%lx, size 0x%lx) extends past flash end (0x%lx)",
        i, (unsigned long)off, (unsigned long)sz, (unsigned long)flash_size);
      ESP_LOGE(TAG, "PT update: %s", err_msg ? err_msg : "past flash end");
      return ESP_ERR_INVALID_SIZE;
    }
  }

  s_pt_state = PT_UPDATE_VERIFIED;
  ESP_LOGI(TAG, "PT update: verified (%d partitions)", num_partitions);
  return ESP_OK;
}

esp_err_t partition_table_update_commit(void) {
  if (s_pt_state != PT_UPDATE_VERIFIED || !s_pt_staging) {
    ESP_LOGE(TAG, "PT update: commit called from state %d", (int)s_pt_state);
    return ESP_ERR_INVALID_STATE;
  }

  // 1. Read the current PT sector into a backup buffer for best-effort
  //    software-error recovery. This does NOT protect against power loss.
  uint8_t *backup = heap_caps_malloc(PT_SECTOR_SIZE, MALLOC_CAP_SPIRAM);
  if (!backup) {
    ESP_LOGE(TAG, "PT update: backup alloc failed");
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = esp_flash_read(NULL, backup, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PT update: backup read failed: %s", esp_err_to_name(err));
    heap_caps_free(backup);
    return err;
  }

  ESP_LOGW(TAG, "PT update: committing %u bytes to flash 0x%lx (DO NOT POWER OFF)",
    (unsigned)s_pt_staging_len, (unsigned long)PT_FLASH_OFFSET);

  // 2. Erase the PT sector.
  err = esp_flash_erase_region(NULL, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PT update: erase failed: %s", esp_err_to_name(err));
    // Best-effort restore (will likely also fail since we couldn't erase).
    (void)esp_flash_write(NULL, backup, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
    heap_caps_free(backup);
    s_pt_state = PT_UPDATE_ERROR;
    return err;
  }

  // 3. Write the candidate PT.
  err = esp_flash_write(NULL, s_pt_staging, PT_FLASH_OFFSET, s_pt_staging_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PT update: write failed: %s, attempting backup restore",
      esp_err_to_name(err));
    esp_err_t restore_err = esp_flash_erase_region(NULL, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
    if (restore_err == ESP_OK) {
      restore_err = esp_flash_write(NULL, backup, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
    }
    heap_caps_free(backup);
    s_pt_state = PT_UPDATE_ERROR;
    return restore_err == ESP_OK ? ESP_FAIL : restore_err;
  }

  // 4. Read back and verify byte-for-byte against the staging buffer.
  uint8_t *readback = heap_caps_malloc(s_pt_staging_len, MALLOC_CAP_SPIRAM);
  if (!readback) {
    ESP_LOGE(TAG, "PT update: readback alloc failed; commit unverified");
    heap_caps_free(backup);
    s_pt_state = PT_UPDATE_COMMITTED;  // Trust that write returned OK.
    pt_release_staging();
    return ESP_OK;
  }
  err = esp_flash_read(NULL, readback, PT_FLASH_OFFSET, s_pt_staging_len);
  if (err != ESP_OK || memcmp(readback, s_pt_staging, s_pt_staging_len) != 0) {
    ESP_LOGE(TAG, "PT update: readback mismatch, attempting backup restore");
    esp_err_t restore_err = esp_flash_erase_region(NULL, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
    if (restore_err == ESP_OK) {
      restore_err = esp_flash_write(NULL, backup, PT_FLASH_OFFSET, PT_SECTOR_SIZE);
    }
    heap_caps_free(readback);
    heap_caps_free(backup);
    s_pt_state = PT_UPDATE_ERROR;
    return restore_err == ESP_OK ? ESP_FAIL : restore_err;
  }

  heap_caps_free(readback);
  heap_caps_free(backup);
  s_pt_state = PT_UPDATE_COMMITTED;
  pt_release_staging();
  ESP_LOGI(TAG, "PT update: committed and verified");
  return ESP_OK;
}

void partition_table_update_abort(void) {
  pt_release_staging();
  s_pt_state = PT_UPDATE_IDLE;
  ESP_LOGI(TAG, "PT update: aborted");
}

partition_table_update_state_t partition_table_update_get_state(void) {
  return s_pt_state;
}

// ============================================================================
// Raw assets partition write (Phase 0 / v(N+1))
// ============================================================================

static const esp_partition_t *s_raw_assets_partition = NULL;
static uint8_t *s_raw_erase_bitmap = NULL;
static size_t s_raw_erase_bitmap_bytes = 0;
static const size_t RAW_SECTOR_SIZE = 0x1000;

static esp_err_t raw_assets_lookup(void) {
  if (s_raw_assets_partition) return ESP_OK;
  s_raw_assets_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "assets");
  if (!s_raw_assets_partition) {
    ESP_LOGE(TAG, "raw_assets: no `assets` partition found");
    return ESP_ERR_NOT_FOUND;
  }
  return ESP_OK;
}

static bool raw_bitmap_get(size_t sector_idx) {
  if (!s_raw_erase_bitmap) return false;
  size_t byte = sector_idx >> 3;
  uint8_t mask = 1u << (sector_idx & 7);
  if (byte >= s_raw_erase_bitmap_bytes) return false;
  return (s_raw_erase_bitmap[byte] & mask) != 0;
}

static void raw_bitmap_set(size_t sector_idx) {
  if (!s_raw_erase_bitmap) return;
  size_t byte = sector_idx >> 3;
  uint8_t mask = 1u << (sector_idx & 7);
  if (byte < s_raw_erase_bitmap_bytes) s_raw_erase_bitmap[byte] |= mask;
}

esp_err_t raw_assets_write_start(void) {
  esp_err_t err = raw_assets_lookup();
  if (err != ESP_OK) return err;
  if (s_raw_erase_bitmap) {
    // Already in a session; treat as no-op.
    return ESP_OK;
  }
  size_t num_sectors = s_raw_assets_partition->size / RAW_SECTOR_SIZE;
  s_raw_erase_bitmap_bytes = (num_sectors + 7) / 8;
  s_raw_erase_bitmap = heap_caps_calloc(1, s_raw_erase_bitmap_bytes, MALLOC_CAP_SPIRAM);
  if (!s_raw_erase_bitmap) {
    ESP_LOGE(TAG, "raw_assets: failed to allocate %u-byte erase bitmap",
      (unsigned)s_raw_erase_bitmap_bytes);
    s_raw_erase_bitmap_bytes = 0;
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "raw_assets: session started (%u sectors, %u-byte bitmap)",
    (unsigned)num_sectors, (unsigned)s_raw_erase_bitmap_bytes);
  return ESP_OK;
}

esp_err_t raw_assets_write_chunk(uint32_t offset, const uint8_t *data, size_t len) {
  esp_err_t err = raw_assets_write_start();  // Lazy start for ergonomics.
  if (err != ESP_OK) return err;
  if (!data || len == 0) return ESP_ERR_INVALID_ARG;
  if (offset + len > s_raw_assets_partition->size) {
    ESP_LOGE(TAG, "raw_assets: chunk past end (offset 0x%lx + %u > 0x%lx)",
      (unsigned long)offset, (unsigned)len,
      (unsigned long)s_raw_assets_partition->size);
    return ESP_ERR_INVALID_SIZE;
  }
  // Erase any sectors we touch that are not yet erased in this session.
  size_t first_sector = offset / RAW_SECTOR_SIZE;
  size_t last_sector = (offset + len - 1) / RAW_SECTOR_SIZE;
  for (size_t s = first_sector; s <= last_sector; s++) {
    if (raw_bitmap_get(s)) continue;
    err = esp_partition_erase_range(s_raw_assets_partition,
      s * RAW_SECTOR_SIZE, RAW_SECTOR_SIZE);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "raw_assets: erase sector %u failed: %s",
        (unsigned)s, esp_err_to_name(err));
      return err;
    }
    raw_bitmap_set(s);
  }
  err = esp_partition_write(s_raw_assets_partition, offset, data, len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "raw_assets: write at 0x%lx failed: %s",
      (unsigned long)offset, esp_err_to_name(err));
  }
  return err;
}

void raw_assets_write_finalize(const char *checksum_or_null) {
  if (s_raw_erase_bitmap) {
    heap_caps_free(s_raw_erase_bitmap);
    s_raw_erase_bitmap = NULL;
    s_raw_erase_bitmap_bytes = 0;
    ESP_LOGI(TAG, "raw_assets: session ended");
  }

  // Persist the 8-hex assets checksum if the caller passed one. This is the
  // RAW (system-update) counterpart to the classic ASSETS-OTA COMMIT path,
  // which already calls version_set_assets_checksum(). Without this, the
  // scene-manager factory-preset merge can't tell that the assets blob
  // changed and won't pick up newly-shipped presets.
  if (checksum_or_null && checksum_or_null[0] != '\0') {
    if (strlen(checksum_or_null) != 8) {
      ESP_LOGW(TAG, "raw_assets: ignoring checksum (expected 8 hex chars, got '%s')",
        checksum_or_null);
      return;
    }
    for (int i = 0; i < 8; i++) {
      if (!isxdigit((unsigned char)checksum_or_null[i])) {
        ESP_LOGW(TAG, "raw_assets: ignoring non-hex checksum '%s'",
          checksum_or_null);
        return;
      }
    }
    esp_err_t err = version_set_assets_checksum(checksum_or_null);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "raw_assets: failed to persist checksum: %s",
        esp_err_to_name(err));
    }
  }
}

