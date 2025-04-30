#ifndef _APP_SETTINGS_H
#define _APP_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Initialize the settings module
esp_err_t app_settings_init(void);

// Save a uint16_t value
esp_err_t app_settings_save_u16(const char* key, uint16_t value);

// Load a uint16_t value
esp_err_t app_settings_load_u16(const char* key, uint16_t* value);

// Save a uint32_t value
esp_err_t app_settings_save_u32(const char* key, uint32_t value);

// Load a uint32_t value
esp_err_t app_settings_load_u32(const char* key, uint32_t* value);

// Save a bool value
esp_err_t app_settings_save_bool(const char* key, bool value);

// Load a bool value
esp_err_t app_settings_load_bool(const char* key, bool* value);

// Save a string value
esp_err_t app_settings_save_str(const char* key, const char* value);

// Load a string value
esp_err_t app_settings_load_str(const char* key, char* value, size_t max_len);

// Erase a key
esp_err_t app_settings_erase_key(const char* key);

// Erase all settings
esp_err_t app_settings_erase_all(void);

#endif /* _APP_SETTINGS_H */ 