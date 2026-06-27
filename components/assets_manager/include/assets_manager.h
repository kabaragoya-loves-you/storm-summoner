#ifndef ASSETS_MANAGER_H
#define ASSETS_MANAGER_H

#include "assets_types.h"
#include "esp_err.h"
#include <stdbool.h>

// Mount points for the two LittleFS partitions.
//   ASSETS_BASE_PATH  -> read-only, replaced wholesale by ASSETS OTA
//   USERDATA_BASE_PATH -> read-write, persistent across firmware/assets updates
//
// Other components should reference these macros instead of hard-coding paths
// so a future move (e.g. /assets -> /shared) only touches this header.
#define ASSETS_BASE_PATH    "/assets"
#define USERDATA_BASE_PATH  "/userdata"
#define ASSETS_PARTITION    "assets"
#define USERDATA_PARTITION  "userdata"

// Helper function to convert MIDI TRS type to transmit mode
// (maps to midi_transmit_mode_t values from midi_out_uart.h)
static inline int assets_trs_type_to_transmit_mode(midi_trs_type_t trs_type) {
  switch (trs_type) {
    case MIDI_TRS_TYPE_A:    return 1;  // MIDI_TRANSMIT_TYPE_A
    case MIDI_TRS_TYPE_B:    return 2;  // MIDI_TRANSMIT_TYPE_B
    case MIDI_TRS_TYPE_TS:   return 3;  // MIDI_TRANSMIT_TS
    case MIDI_TRS_TYPE_BOTH: return 0;  // MIDI_TRANSMIT_BOTH
    case MIDI_TRS_UNKNOWN:
    default:                 return 0;  // MIDI_TRANSMIT_BOTH (fallback)
  }
}


/**
 * Initialize the assets manager
 * - Mounts the RO `assets` LittleFS partition
 * - Mounts (formatting on first mount) the RW `userdata` LittleFS partition
 * - Loads and parses manifest.json
 *
 * Returns ESP_OK on success. If the `userdata` partition mount fails, init
 * logs an ESP_LOGE but still returns ESP_OK so the rest of the system can
 * boot in degraded mode. Callers can check assets_userdata_available() to
 * decide whether to attempt writes.
 */
esp_err_t assets_manager_init(void);

/**
 * @return true if the `userdata` partition is mounted and writable.
 *         false if the partition is missing (degraded boot) - in which case
 *         all writes under USERDATA_BASE_PATH will fail at the fopen/mkdir
 *         layer and callers should expect that.
 */
bool assets_userdata_available(void);

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
 * Resolver for a gating CC's current value (0-127). Implemented by the action
 * layer (action_get_cc_value) and registered via assets_set_cc_value_provider.
 */
typedef uint8_t (*assets_cc_value_fn)(uint8_t cc_num);

/**
 * Register the gating-CC value provider used by the effective-control
 * resolution (x_variants). Pass NULL to disable variant resolution (all
 * lookups then use the base control). Call once during init with
 * action_get_cc_value.
 */
void assets_set_cc_value_provider(assets_cc_value_fn fn);

/**
 * Get control by CC number
 * Returns pointer to control or NULL if not found
 */
const midi_control_t *assets_get_control_by_cc(const device_def_t *device, uint8_t cc_num);

/**
 * True if `cc_num` is referenced as a gating CC by any control's x_variants
 * constraint on this device (i.e. changing it can alter other controls'
 * effective options).
 */
bool assets_cc_is_gating(const device_def_t *device, uint8_t cc_num);

/**
 * True if `cc_num` resolves to a no-op (x_noop) in the current gating state,
 * i.e. the CC should be hidden/not rendered. Uses the registered gating-CC
 * value provider. Returns false if the CC is not defined or no provider is set
 * and the base control is not itself x_noop.
 */
bool assets_cc_is_noop(const device_def_t *device, uint8_t cc_num);

/**
 * True if `cc_num` is flagged x_mandatory in the device definition, meaning the
 * scene must always carry a default value for it (the gate/mode CC).
 */
bool assets_cc_is_mandatory(const device_def_t *device, uint8_t cc_num);

/**
 * Resolve the effective control for a CC, applying the first matching
 * x_variants override based on the current value of its gating CC.
 *
 * @param device    Device definition
 * @param cc_num    CC number to resolve
 * @param get_value Gating-CC value resolver (NULL = use registered provider)
 * @param scratch   Caller-provided buffer that receives the merged control
 *                  when a variant matches. Must remain valid while the
 *                  returned pointer is used.
 * @return The base control when no variant matches (or none defined), or
 *         `scratch` populated with the matched variant's overrides. NULL if
 *         the CC is not defined on the device.
 */
const midi_control_t *assets_get_effective_control(const device_def_t *device,
  uint8_t cc_num, assets_cc_value_fn get_value, midi_control_t *scratch);

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

/**
 * Get MIDI TRS wiring type for device
 * Returns the TRS type (TYPE_A, TYPE_B, TYPE_TS, or UNKNOWN)
 */
midi_trs_type_t assets_get_trs_type(const device_def_t *device);

/**
 * Get CC name from device profile
 * Returns control name or "Undefined" if CC not in profile, NULL if device is NULL
 */
const char *assets_get_cc_name(const device_def_t *device, uint8_t cc_num);

/**
 * Get the index of the discrete value that matches or contains the given value
 * Returns index (0 to discrete_count-1) or -1 if not found or not discrete
 */
int assets_get_discrete_index(const device_def_t *device, uint8_t cc_num, uint16_t value);

/**
 * Get discrete value name for a given MIDI value
 * Returns name string or NULL if not discrete or not found
 */
const char *assets_get_discrete_name(const device_def_t *device, uint8_t cc_num, uint16_t value);

/**
 * Snap a value to the nearest discrete value for a CC
 * Returns snapped value, or original value if CC has no discrete values
 */
uint16_t assets_snap_to_discrete(const device_def_t *device, uint8_t cc_num, uint16_t value);

/**
 * Get next discrete value (for cycling through options)
 * Returns next value, wrapping to first if at end
 * Returns current value if CC has no discrete values
 */
uint16_t assets_get_next_discrete(const device_def_t *device, uint8_t cc_num, uint16_t current);

/**
 * Get previous discrete value (for cycling through options)
 * Returns previous value, wrapping to last if at start
 * Returns current value if CC has no discrete values
 */
uint16_t assets_get_prev_discrete(const device_def_t *device, uint8_t cc_num, uint16_t current);

/**
 * Check if value is valid for a CC according to device min/max
 * Returns true if valid or if device/CC not found (permissive)
 */
bool assets_validate_cc_value(const device_def_t *device, uint8_t cc_num, uint16_t value);

/**
 * Clamp value to device min/max for a CC
 * Returns clamped value, or original if device/CC not found
 */
uint16_t assets_clamp_cc_value(const device_def_t *device, uint8_t cc_num, uint16_t value);

/**
 * Check if a CC has discrete values defined
 */
bool assets_cc_has_discrete_values(const device_def_t *device, uint8_t cc_num);

/**
 * Validate a device JSON file on disk (duplicate controlChangeNumber, etc.).
 * @return ESP_OK if acceptable, ESP_ERR_INVALID_STATE if duplicate CC numbers
 */
esp_err_t assets_validate_device_json_file(const char *filepath);

/**
 * Reload the device manifest from LittleFS
 * Re-scans and parses manifest.json
 * @return ESP_OK on success
 */
esp_err_t assets_manager_reload_manifest(void);

/**
 * Rebuild the device manifest by scanning /assets/devices/ directory
 * Scans all vendor subdirectories for .json device files and generates
 * a new manifest.json. Call this after adding/removing device files.
 * @return ESP_OK on success
 */
esp_err_t assets_rebuild_manifest(void);

/**
 * Reload a specific device definition
 * Invalidates cache and reloads from JSON
 * @param slug Device slug to reload
 * @return ESP_OK on success
 */
esp_err_t assets_manager_reload_device(const char *slug);

/**
 * Resolve the on-disk JSON path for a manifest device entry.
 * RO entries: /assets/devices/<manifest path>
 * RW entries: /userdata/<manifest path>
 */
esp_err_t assets_manifest_device_json_path(const manifest_device_t *dev,
  char *out, size_t out_len);

/**
 * Sync all device profiles and scenes to MSC RAM volume
 * Helper function for USB manager to populate MSC volume
 * @return ESP_OK on success
 */
esp_err_t assets_manager_sync_to_msc(void);

/**
 * Get count of unique vendors in the manifest
 * @return Number of unique vendors
 */
uint32_t assets_get_vendor_count(void);

/**
 * Get vendor name by index (alphabetically sorted)
 * @param idx Index (0 to vendor_count-1)
 * @return Vendor name or NULL if index out of range
 */
const char* assets_get_vendor_by_index(uint32_t idx);

/**
 * Get device count for a specific vendor
 * @param vendor Vendor name to filter by
 * @return Number of devices for this vendor
 */
uint32_t assets_get_device_count_for_vendor(const char* vendor);

/**
 * Get device info by vendor and index within that vendor
 * @param vendor Vendor name to filter by
 * @param idx Index within vendor's devices (0 to count-1)
 * @param slug Output: device slug
 * @param name Output: device display name
 * @return ESP_OK on success
 */
esp_err_t assets_get_device_for_vendor(const char* vendor, uint32_t idx,
  const char** slug, const char** name);

/**
 * RW user pedals under /userdata/devices/user/ (slug prefix user.).
 */
uint32_t assets_get_user_device_count(void);

esp_err_t assets_get_user_device_by_index(uint32_t idx,
  const char** slug, const char** name);

/**
 * Get manifest device entry by slug
 * Returns a pointer to the manifest_device_t or NULL if not found
 * This is a lightweight lookup that doesn't load the full device JSON
 */
const manifest_device_t *assets_get_manifest_device(const char *slug);

/**
 * Menu label for a pedal: "User: {displayName}" for RW user pedals, else displayName.
 * Uses "(no pedal)" when display_name is NULL or empty.
 */
void assets_format_pedal_menu_label(const char *slug, const char *display_name,
  char *out, size_t out_len);

#endif // ASSETS_MANAGER_H

