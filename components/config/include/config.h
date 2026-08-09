#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define USER_HANDLE_MAX_LEN 12

// Device mode: single device for all scenes vs per-scene device selection
typedef enum {
  DEVICE_MODE_SINGLE = 0,    // One device for all scenes (default)
  DEVICE_MODE_PER_SCENE = 1  // Allow per-scene device overrides
} device_mode_t;

// Initialize config module (loads settings from NVS)
esp_err_t config_init(void);

// Preset wrap setting
// When true: preset/program numbers wrap around at boundaries
// When false: preset/program numbers clamp at boundaries
bool config_get_preset_wrap(void);
esp_err_t config_set_preset_wrap(bool wrap);

// Persist scene setting
// When true: the current scene index is saved to NVS and restored on boot
// When false: device always boots to scene 1
bool config_get_persist_scene(void);
esp_err_t config_set_persist_scene(bool persist);

// Last scene index (used when persist_scene is enabled)
uint8_t config_get_last_scene(void);
esp_err_t config_set_last_scene(uint8_t scene_index);

// Device mode setting
// DEVICE_MODE_SINGLE: all scenes use the global device/channel
// DEVICE_MODE_PER_SCENE: scenes can override the global device/channel
device_mode_t config_get_device_mode(void);
esp_err_t config_set_device_mode(device_mode_t mode);

// Incoming CC mirror setting
// When true: incoming USB/UART CC messages on the device's own MIDI channel
// update the s_last_cc_values cache, so mode-gating CCs set by another
// controller are reflected in variant resolution.
// When false (default): incoming CC is ignored for mode tracking.
bool config_get_cc_mirror(void);
esp_err_t config_set_cc_mirror(bool enabled);

// User handle (stored in NVS, read on demand)
esp_err_t config_get_user_handle(char* buf, size_t len);
esp_err_t config_set_user_handle(const char* handle);

#endif // CONFIG_H

