#include "ui_module_settings.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "UI_SETTINGS";

// Registration entry
typedef struct {
  const char* module_name;
  ui_module_setting_t* settings;
  size_t count;
} module_settings_entry_t;

// Global registry
static module_settings_entry_t s_registry[UI_MAX_MODULES_WITH_SETTINGS];
static size_t s_registry_count = 0;

// Find entry by module name
static module_settings_entry_t* find_entry(const char* module_name) {
  for (size_t i = 0; i < s_registry_count; i++) {
    if (strcmp(s_registry[i].module_name, module_name) == 0) {
      return &s_registry[i];
    }
  }
  return NULL;
}

// Find setting in a module
static ui_module_setting_t* find_setting(module_settings_entry_t* entry, 
                                          const char* setting_name) {
  if (!entry) return NULL;
  for (size_t i = 0; i < entry->count; i++) {
    if (strcmp(entry->settings[i].name, setting_name) == 0) {
      return &entry->settings[i];
    }
  }
  return NULL;
}

void ui_module_register_settings(const char* module_name,
                                  ui_module_setting_t* settings,
                                  size_t count) {
  if (!module_name || !settings || count == 0) {
    ESP_LOGW(TAG, "Invalid registration parameters");
    return;
  }
  
  // Check if already registered
  module_settings_entry_t* existing = find_entry(module_name);
  if (existing) {
    // Update existing entry
    existing->settings = settings;
    existing->count = count;
    ESP_LOGD(TAG, "Updated settings for module '%s' (%d settings)", 
             module_name, (int)count);
    return;
  }
  
  // Add new entry
  if (s_registry_count >= UI_MAX_MODULES_WITH_SETTINGS) {
    ESP_LOGE(TAG, "Module settings registry full");
    return;
  }
  
  s_registry[s_registry_count].module_name = module_name;
  s_registry[s_registry_count].settings = settings;
  s_registry[s_registry_count].count = count;
  s_registry_count++;
  
  ESP_LOGD(TAG, "Registered %d settings for module '%s'", (int)count, module_name);
}

void ui_module_unregister_settings(const char* module_name) {
  if (!module_name) return;
  
  for (size_t i = 0; i < s_registry_count; i++) {
    if (strcmp(s_registry[i].module_name, module_name) == 0) {
      // Shift remaining entries
      for (size_t j = i; j < s_registry_count - 1; j++) {
        s_registry[j] = s_registry[j + 1];
      }
      s_registry_count--;
      ESP_LOGD(TAG, "Unregistered settings for module '%s'", module_name);
      return;
    }
  }
}

ui_module_setting_t* ui_module_get_settings(const char* module_name, size_t* count) {
  module_settings_entry_t* entry = find_entry(module_name);
  if (!entry) {
    if (count) *count = 0;
    return NULL;
  }
  if (count) *count = entry->count;
  return entry->settings;
}

bool ui_module_set_setting(const char* module_name, const char* setting_name,
                            const char* value) {
  module_settings_entry_t* entry = find_entry(module_name);
  if (!entry) {
    ESP_LOGW(TAG, "Module '%s' not found", module_name);
    return false;
  }
  
  ui_module_setting_t* setting = find_setting(entry, setting_name);
  if (!setting) {
    ESP_LOGW(TAG, "Setting '%s' not found in module '%s'", setting_name, module_name);
    return false;
  }
  
  switch (setting->type) {
    case UI_SETTING_BOOL: {
      bool val;
      if (strcmp(value, "true") == 0 || strcmp(value, "on") == 0 || 
          strcmp(value, "1") == 0) {
        val = true;
      } else if (strcmp(value, "false") == 0 || strcmp(value, "off") == 0 || 
                 strcmp(value, "0") == 0) {
        val = false;
      } else {
        ESP_LOGW(TAG, "Invalid boolean value: %s", value);
        return false;
      }
      *(bool*)setting->value_ptr = val;
      break;
    }
    case UI_SETTING_U8: {
      int val = atoi(value);
      if (val < 0 || val > 255) {
        ESP_LOGW(TAG, "Value out of range for u8: %s", value);
        return false;
      }
      *(uint8_t*)setting->value_ptr = (uint8_t)val;
      break;
    }
    case UI_SETTING_U16: {
      int val = atoi(value);
      if (val < 0 || val > 65535) {
        ESP_LOGW(TAG, "Value out of range for u16: %s", value);
        return false;
      }
      *(uint16_t*)setting->value_ptr = (uint16_t)val;
      break;
    }
    case UI_SETTING_I16: {
      int val = atoi(value);
      if (val < -32768 || val > 32767) {
        ESP_LOGW(TAG, "Value out of range for i16: %s", value);
        return false;
      }
      *(int16_t*)setting->value_ptr = (int16_t)val;
      break;
    }
    case UI_SETTING_I32: {
      long val = strtol(value, NULL, 10);
      *(int32_t*)setting->value_ptr = (int32_t)val;
      break;
    }
    case UI_SETTING_FLOAT: {
      float val = atof(value);
      *(float*)setting->value_ptr = val;
      break;
    }
    case UI_SETTING_ENUM: {
      if (!setting->enum_values) {
        ESP_LOGW(TAG, "Enum setting '%s' has no values defined", setting_name);
        return false;
      }
      // Validate against enum values
      bool valid = false;
      for (const char** p = setting->enum_values; *p != NULL; p++) {
        if (strcmp(*p, value) == 0) {
          valid = true;
          break;
        }
      }
      if (!valid) {
        ESP_LOGW(TAG, "Invalid enum value '%s' for setting '%s'", value, setting_name);
        // List valid values
        printf("Valid values: ");
        for (const char** p = setting->enum_values; *p != NULL; p++) {
          printf("%s%s", *p, *(p + 1) ? ", " : "\n");
        }
        return false;
      }
      // Copy string to buffer (assumes value_ptr points to char array)
      strncpy((char*)setting->value_ptr, value, 31);
      ((char*)setting->value_ptr)[31] = '\0';
      break;
    }
    default:
      ESP_LOGW(TAG, "Unknown setting type");
      return false;
  }
  
  // Call on_change callback if defined
  if (setting->on_change) {
    setting->on_change();
  }
  
  ESP_LOGI(TAG, "Set %s.%s = %s", module_name, setting_name, value);
  return true;
}

bool ui_module_get_setting_str(const char* module_name, const char* setting_name,
                                char* buffer, size_t buffer_size) {
  module_settings_entry_t* entry = find_entry(module_name);
  if (!entry) return false;
  
  ui_module_setting_t* setting = find_setting(entry, setting_name);
  if (!setting) return false;
  
  switch (setting->type) {
    case UI_SETTING_BOOL:
      snprintf(buffer, buffer_size, "%s", *(bool*)setting->value_ptr ? "true" : "false");
      break;
    case UI_SETTING_U8:
      snprintf(buffer, buffer_size, "%u", *(uint8_t*)setting->value_ptr);
      break;
    case UI_SETTING_U16:
      snprintf(buffer, buffer_size, "%u", *(uint16_t*)setting->value_ptr);
      break;
    case UI_SETTING_I16:
      snprintf(buffer, buffer_size, "%d", *(int16_t*)setting->value_ptr);
      break;
    case UI_SETTING_I32:
      snprintf(buffer, buffer_size, "%ld", (long)*(int32_t*)setting->value_ptr);
      break;
    case UI_SETTING_FLOAT:
      snprintf(buffer, buffer_size, "%.2f", *(float*)setting->value_ptr);
      break;
    case UI_SETTING_ENUM:
      snprintf(buffer, buffer_size, "%s", (char*)setting->value_ptr);
      break;
    default:
      return false;
  }
  return true;
}

size_t ui_module_list_registered(const char** names, size_t max_count) {
  size_t count = s_registry_count < max_count ? s_registry_count : max_count;
  for (size_t i = 0; i < count; i++) {
    names[i] = s_registry[i].module_name;
  }
  return count;
}

