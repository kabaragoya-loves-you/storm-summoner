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

// ============================================================================
// Partition table OTA (Phase 0 / v(N+1))
// ============================================================================
//
// Replaces the partition table at flash offset 0x8000 with a candidate buffered
// in PSRAM. UNSUPPORTED in IDF; relies on CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED.
// Power loss inside the ~50 ms erase+write window will brick the device.
// The PSRAM backup of the current PT is used to attempt restoration on
// software write failure only; it is not a power-loss safety net.
//
// Lifecycle: start -> write... -> verify -> commit (or abort to release buffer
// without committing). verify must succeed before commit will proceed.

typedef enum {
  PT_UPDATE_IDLE,
  PT_UPDATE_RECEIVING,    // staging buffer allocated, write() in progress
  PT_UPDATE_VERIFIED,     // candidate PT passed esp_partition_table_verify
  PT_UPDATE_COMMITTED,    // flashed, awaiting reboot
  PT_UPDATE_ERROR
} partition_table_update_state_t;

/**
 * Begin a partition-table update. Allocates a PSRAM staging buffer of total_len
 * bytes. If an upload is already in progress it is aborted first.
 * @param total_len Expected size of the partition table image in bytes
 *                  (typically 0xC00 + 0x20 MD5 footer; must be <= 0x1000).
 * @return ESP_OK on success
 */
esp_err_t partition_table_update_start(size_t total_len);

/**
 * Append a chunk to the partition-table staging buffer.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not in RECEIVING state
 */
esp_err_t partition_table_update_write(const uint8_t *data, size_t len);

/**
 * Verify the staged partition table:
 *   - byte length matches what was advertised in start()
 *   - esp_partition_table_verify() passes (magic numbers, MD5 footer, sizes)
 *   - all partition entries fit within the chip's flash size
 *   - no entry overlaps the bootloader region (offset < 0x10000)
 * On success the state advances to PT_UPDATE_VERIFIED and commit is unlocked.
 * On failure the staging buffer is preserved for diagnostic reads but commit
 * will refuse.
 * @param err_msg Optional output buffer for a human-readable failure reason
 * @param err_msg_size Size of err_msg buffer
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE / ESP_ERR_INVALID_CRC etc.
 */
esp_err_t partition_table_update_verify(char *err_msg, size_t err_msg_size);

/**
 * Commit the verified partition table to flash:
 *   1. Read current PT sector into a separate PSRAM backup buffer.
 *   2. esp_flash_erase_range(NULL, 0x8000, 0x1000).
 *   3. esp_flash_write(NULL, staging, 0x8000, total_len).
 *   4. Read back and verify byte-for-byte. On mismatch, attempt to write the
 *      backup buffer back as a best-effort recovery (may also fail).
 * Caller MUST present a confirmation gate to the user before invoking this -
 * the ~50 ms write window is the only irreducible brick risk in the entire
 * partition-split rollout.
 * @return ESP_OK on verified commit; ESP_FAIL if recovery succeeded;
 *         ESP_ERR_INVALID_STATE if not verified; otherwise the underlying
 *         esp_flash_* error code (device may be bricked at this point).
 */
esp_err_t partition_table_update_commit(void);

/**
 * Release the staging buffer without committing. Safe to call from any state.
 */
void partition_table_update_abort(void);

/**
 * Get the current state of the partition-table update state machine.
 */
partition_table_update_state_t partition_table_update_get_state(void);

// ============================================================================
// Raw assets partition write (Phase 0 / v(N+1))
// ============================================================================
//
// Allows the host to perform offset-addressed writes to the existing `assets`
// partition. Used by the v(N+2) deployment flow to seed the new shared assets
// blob into the first 8 MB of the still-10-MB partition before the partition
// table swap. Each chunk lazily erases any sectors it overlaps that have not
// already been erased in this session (tracked in a PSRAM bitmap).
//
// Lifecycle: any call to chunk() implicitly starts a session if none is
// active. finalize() releases the bitmap. start()/finalize() are exposed so
// callers can explicitly bracket sessions if desired.

/**
 * Begin a raw assets write session. Allocates the per-sector erase bitmap in
 * PSRAM. Idempotent; calling again while a session is active is a no-op.
 * @return ESP_OK on success
 */
esp_err_t raw_assets_write_start(void);

/**
 * Write a chunk of bytes to the assets partition at `offset`. Any 4 KB sectors
 * in [offset, offset+len) that have not been erased yet in this session are
 * erased now. Subsequent writes that touch the same sectors do not re-erase.
 * @param offset Byte offset within the assets partition.
 * @param data   Source bytes.
 * @param len    Number of bytes to write.
 * @return ESP_OK on success
 */
esp_err_t raw_assets_write_chunk(uint32_t offset, const uint8_t *data, size_t len);

/**
 * End a raw assets write session. Releases the erase bitmap. After this call,
 * subsequent chunk() calls will allow already-erased sectors to be re-erased
 * (because the bitmap is gone). Safe to call without a matching start().
 *
 * @param checksum_or_null Optional 8-hex assets checksum. When non-NULL and
 *   well-formed, it is persisted to NVS via version_set_assets_checksum()
 *   so the scene manager's factory-preset merge can detect the new assets
 *   blob on next boot. Pass NULL to skip checksum bookkeeping (callers that
 *   are aborting / cancelling a session).
 */
void raw_assets_write_finalize(const char *checksum_or_null);

#endif /* _FIRMWARE_UPDATE_H */

