#ifndef _APP_SETTINGS_H
#define _APP_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "nvs_flash.h"
#include "cJSON.h"

// Abstracted error codes - use these instead of NVS-specific errors
#define APP_SETTINGS_OK ESP_OK
#define APP_SETTINGS_ERR_NOT_FOUND ESP_ERR_NVS_NOT_FOUND
#define APP_SETTINGS_ERR_INVALID_ARG ESP_ERR_INVALID_ARG
#define APP_SETTINGS_ERR_NO_MEM ESP_ERR_NO_MEM
#define APP_SETTINGS_ERR_INVALID_STATE ESP_ERR_INVALID_STATE

// Initialize the settings module
esp_err_t app_settings_init(void);

// Basic type functions
esp_err_t app_settings_save_u8(const char* key, uint8_t value);
esp_err_t app_settings_load_u8(const char* key, uint8_t* value);
esp_err_t app_settings_save_u16(const char* key, uint16_t value);
esp_err_t app_settings_load_u16(const char* key, uint16_t* value);
esp_err_t app_settings_save_u32(const char* key, uint32_t value);
esp_err_t app_settings_load_u32(const char* key, uint32_t* value);
esp_err_t app_settings_save_bool(const char* key, bool value);
esp_err_t app_settings_load_bool(const char* key, bool* value);
esp_err_t app_settings_save_str(const char* key, const char* value);
esp_err_t app_settings_load_str(const char* key, char* value, size_t max_len);

// Binary data functions
esp_err_t app_settings_save_blob(const char* key, const void* value, size_t length);
esp_err_t app_settings_load_blob(const char* key, void* value, size_t max_length, size_t* actual_length);

// Structure handling functions
#define APP_SETTINGS_SAVE_STRUCT(key, struct_ptr) \
  app_settings_save_blob(key, struct_ptr, sizeof(*(struct_ptr)))

#define APP_SETTINGS_LOAD_STRUCT(key, struct_ptr) \
  app_settings_load_blob(key, struct_ptr, sizeof(*(struct_ptr)), NULL)

// Array handling functions
#define APP_SETTINGS_SAVE_ARRAY(key, array_ptr, element_count) \
  app_settings_save_blob(key, array_ptr, sizeof(*(array_ptr)) * (element_count))

#define APP_SETTINGS_LOAD_ARRAY(key, array_ptr, max_elements, actual_elements) \
  app_settings_load_blob(key, array_ptr, sizeof(*(array_ptr)) * (max_elements), \
              (size_t*)(actual_elements))

// Erase functions
esp_err_t app_settings_erase_key(const char* key);
esp_err_t app_settings_erase_all(void);

// JSON export/import functions
// Returns a cJSON object with all settings (caller must free with cJSON_Delete)
cJSON* app_settings_export_json(void);

// Import settings from a cJSON object
// Returns number of keys imported, or -1 on error
int app_settings_import_json(const cJSON* json);

// Export settings as JSON string
// Returns the JSON string (caller must free with cJSON_free)
// If pretty is true, output is formatted with indentation
char* app_settings_export_json_string(bool pretty);

#endif /* _APP_SETTINGS_H */ 