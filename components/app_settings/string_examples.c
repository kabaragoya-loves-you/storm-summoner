#include <stddef.h>    // For size_t
#include <stdbool.h>   // For bool, true, false
#include <string.h>    // For strnlen, strncpy
#include "esp_err.h"   // For esp_err_t, ESP_OK, ESP_ERR_* constants
#include "esp_log.h"   // For ESP_LOGI, ESP_LOGW, ESP_LOGE
#include "nvs.h"       // For NVS-specific error codes
#include "app_settings.h"  // For app_settings_* functions

#define TAG "string_samples"
#define MAX_STRING_LENGTH 128

// Structure to hold a string setting
typedef struct {
  char value[MAX_STRING_LENGTH];
  bool is_set;
} string_setting_t;

// Function to load a string setting
esp_err_t load_string_setting(const char* key, string_setting_t* setting) {
  char buffer[MAX_STRING_LENGTH];
  esp_err_t ret = app_settings_load_str(key, buffer, sizeof(buffer));
  
  if (ret == ESP_OK) {
    // Verify the string is properly null-terminated
    if (strnlen(buffer, MAX_STRING_LENGTH) < MAX_STRING_LENGTH) {
      strncpy(setting->value, buffer, MAX_STRING_LENGTH - 1);
      setting->value[MAX_STRING_LENGTH - 1] = '\0';  // Ensure null termination
      setting->is_set = true;
      return ESP_OK;
    }
    return ESP_ERR_INVALID_SIZE;
  }
  
  setting->is_set = false;
  return ret;
}

// Function to save a string setting
esp_err_t save_string_setting(const char* key, const char* value) {
  // Verify the string isn't too long
  if (strnlen(value, MAX_STRING_LENGTH) >= MAX_STRING_LENGTH) {
    return ESP_ERR_INVALID_SIZE;
  }
  
  return app_settings_save_str(key, value);
}

// Example usage
void example_usage(void) {
  string_setting_t my_setting;
  
  // Load a setting
  if (load_string_setting("my_text", &my_setting) == ESP_OK && my_setting.is_set) {
    ESP_LOGI(TAG, "Loaded setting: %s", my_setting.value);
  } else {
    ESP_LOGW(TAG, "Using default value");
    strncpy(my_setting.value, "default value", MAX_STRING_LENGTH - 1);
    my_setting.value[MAX_STRING_LENGTH - 1] = '\0';
  }
  
  // Save a new value
  const char* new_value = "Hello, ESP32!";
  if (save_string_setting("my_text", new_value) == ESP_OK) {
    ESP_LOGI(TAG, "Successfully saved new value");
  }
}

void string_examples(void) {
  // Example of storing a string
  const char* my_string = "Hello, World!";
  esp_err_t ret = app_settings_save_str("greeting", my_string);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save string");
  }

  // Example of loading a string
  char loaded_string[32];  // Must be large enough for your string + null terminator
  ret = app_settings_load_str("greeting", loaded_string, sizeof(loaded_string));
  if (ret == ESP_OK) {
    // loaded_string is now a proper null-terminated C string
    ESP_LOGI(TAG, "Loaded string: %s", loaded_string);
  } else if (ret == ESP_ERR_NVS_INVALID_LENGTH) {
    ESP_LOGE(TAG, "Buffer too small for string");
  } else {
    ESP_LOGE(TAG, "Failed to load string");
  }

  // Example of storing a string that needs escaping
  const char* complex_string = "Line 1\nLine 2\tTabbed";
  char escaped_string[64];
  size_t len = 0;

  // Simple escaping example (you might want more comprehensive escaping)
  for (size_t i = 0; complex_string[i] != '\0' && len < sizeof(escaped_string) - 1; i++) {
    if (complex_string[i] == '\n') {
      escaped_string[len++] = '\\';
      escaped_string[len++] = 'n';
    } else if (complex_string[i] == '\t') {
      escaped_string[len++] = '\\';
      escaped_string[len++] = 't';
    } else {
      escaped_string[len++] = complex_string[i];
    }
  }
  escaped_string[len] = '\0';

  // Store the escaped string
  app_settings_save_str("complex_text", escaped_string);

  // When loading, you'd need to unescape it
  char loaded_escaped[64];
  if (app_settings_load_str("complex_text", loaded_escaped, sizeof(loaded_escaped)) == ESP_OK) {
    char unescaped[64];
    size_t unescaped_len = 0;
    
    for (size_t i = 0; loaded_escaped[i] != '\0' && unescaped_len < sizeof(unescaped) - 1; i++) {
      if (loaded_escaped[i] == '\\' && loaded_escaped[i + 1] == 'n') {
        unescaped[unescaped_len++] = '\n';
        i++;
      } else if (loaded_escaped[i] == '\\' && loaded_escaped[i + 1] == 't') {
        unescaped[unescaped_len++] = '\t';
        i++;
      } else {
        unescaped[unescaped_len++] = loaded_escaped[i];
      }
    }
    unescaped[unescaped_len] = '\0';
    
    // Now unescaped contains the original string with proper newlines and tabs
  }
}