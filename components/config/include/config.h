#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

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

// Flag system setting
// When true: the "Flag Ceremony" action and "Raise the Flag" option are available
// When false: flag-related features are hidden (default)
bool config_get_flag_enabled(void);
esp_err_t config_set_flag_enabled(bool enabled);

#endif // CONFIG_H

