#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "app_settings.h"

#define TAG "structure_examples"

// Example of a complex structure
typedef struct {
  uint8_t version;
  bool enabled;
  uint16_t values[4];
  char name[32];
} complex_settings_t;

// Example of a nested structure
typedef struct {
  uint8_t id;
  complex_settings_t settings;
} nested_settings_t;

// Example of an array of structures
#define MAX_ITEMS 10
typedef struct {
  uint8_t count;
  complex_settings_t items[MAX_ITEMS];
} settings_array_t;

void structure_examples(void) {
  // Example 1: Saving and loading a complex structure
  complex_settings_t settings = {
    .version = 1,
    .enabled = true,
    .values = {100, 200, 300, 400},
    .name = "Test Settings"
  };

  // Save the structure
  if (APP_SETTINGS_SAVE_STRUCT("complex_settings", &settings) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save complex settings");
  }

  // Load the structure
  complex_settings_t loaded_settings;
  if (APP_SETTINGS_LOAD_STRUCT("complex_settings", &loaded_settings) == ESP_OK) {
    ESP_LOGI(TAG, "Loaded complex settings: version=%d, enabled=%d, name=%s",
        loaded_settings.version, loaded_settings.enabled, loaded_settings.name);
  }

  // Example 2: Saving and loading a nested structure
  nested_settings_t nested = {
    .id = 42,
    .settings = settings
  };

  if (APP_SETTINGS_SAVE_STRUCT("nested_settings", &nested) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save nested settings");
  }

  nested_settings_t loaded_nested;
  if (APP_SETTINGS_LOAD_STRUCT("nested_settings", &loaded_nested) == ESP_OK) {
    ESP_LOGI(TAG, "Loaded nested settings: id=%d", loaded_nested.id);
  }

  // Example 3: Saving and loading an array of structures
  settings_array_t array = {
    .count = 3,
    .items = {
      {1, true, {100, 200, 300, 400}, "Item 1"},
      {2, true, {200, 300, 400, 500}, "Item 2"},
      {3, false, {300, 400, 500, 600}, "Item 3"}
    }
  };

  if (APP_SETTINGS_SAVE_ARRAY("settings_array", &array, array.count + 1) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save settings array");
  }

  settings_array_t loaded_array;
  size_t actual_elements;
  if (APP_SETTINGS_LOAD_ARRAY("settings_array", &loaded_array, MAX_ITEMS + 1, &actual_elements) == ESP_OK) {
    ESP_LOGI(TAG, "Loaded array with %d elements", loaded_array.count);
    for (int i = 0; i < loaded_array.count; i++) {
      ESP_LOGI(TAG, "Item %d: version=%d, name=%s", 
          i, loaded_array.items[i].version, loaded_array.items[i].name);
    }
  }

  // Example 4: Using raw blob functions for custom serialization
  uint8_t custom_data[] = {1, 2, 3, 4, 5};
  if (app_settings_save_blob("custom_data", custom_data, sizeof(custom_data)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save custom data");
  }

  uint8_t loaded_custom[10];
  size_t loaded_size;
  if (app_settings_load_blob("custom_data", loaded_custom, sizeof(loaded_custom), &loaded_size) == ESP_OK) {
    ESP_LOGI(TAG, "Loaded %d bytes of custom data", loaded_size);
  }
} 