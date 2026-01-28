#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdlib.h>

#define TAG "app_settings"
#define NVS_NAMESPACE "app_settings"

static nvs_handle_t app_nvs_handle;

esp_err_t app_settings_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Open NVS handle
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &app_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t app_settings_save_u8(const char* key, uint8_t value) {
    esp_err_t ret = nvs_set_u8(app_nvs_handle, key, value);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_u8(const char* key, uint8_t* value) {
    return nvs_get_u8(app_nvs_handle, key, value);
}

esp_err_t app_settings_save_u16(const char* key, uint16_t value) {
    esp_err_t ret = nvs_set_u16(app_nvs_handle, key, value);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_u16(const char* key, uint16_t* value) {
    return nvs_get_u16(app_nvs_handle, key, value);
}

esp_err_t app_settings_save_u32(const char* key, uint32_t value) {
    esp_err_t ret = nvs_set_u32(app_nvs_handle, key, value);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_u32(const char* key, uint32_t* value) {
    return nvs_get_u32(app_nvs_handle, key, value);
}

esp_err_t app_settings_save_bool(const char* key, bool value) {
    esp_err_t ret = nvs_set_u8(app_nvs_handle, key, value ? 1 : 0);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_bool(const char* key, bool* value) {
    uint8_t val;
    esp_err_t ret = nvs_get_u8(app_nvs_handle, key, &val);
    if (ret == ESP_OK) {
        *value = val != 0;
    }
    return ret;
}

esp_err_t app_settings_save_str(const char* key, const char* value) {
    esp_err_t ret = nvs_set_str(app_nvs_handle, key, value);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_str(const char* key, char* value, size_t max_len) {
    size_t required_size;
    esp_err_t ret = nvs_get_str(app_nvs_handle, key, NULL, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }
    if (required_size > max_len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    return nvs_get_str(app_nvs_handle, key, value, &required_size);
}

esp_err_t app_settings_erase_key(const char* key) {
    esp_err_t ret = nvs_erase_key(app_nvs_handle, key);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_erase_all(void) {
    esp_err_t ret = nvs_erase_all(app_nvs_handle);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_save_blob(const char* key, const void* value, size_t length) {
    esp_err_t ret = nvs_set_blob(app_nvs_handle, key, value, length);
    if (ret == ESP_OK) nvs_commit(app_nvs_handle);
    return ret;
}

esp_err_t app_settings_load_blob(const char* key, void* value, size_t max_length, size_t* actual_length) {
    size_t required_size;
    esp_err_t ret = nvs_get_blob(app_nvs_handle, key, NULL, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }
    if (required_size > max_length) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (actual_length) {
        *actual_length = required_size;
    }
    return nvs_get_blob(app_nvs_handle, key, value, &required_size);
}

// ============================================================================
// JSON Export/Import Functions
// ============================================================================

cJSON* app_settings_export_json(void) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    ESP_LOGE(TAG, "Failed to create JSON object");
    return NULL;
  }

  nvs_iterator_t it = NULL;
  esp_err_t err = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    // No keys - return empty object
    return root;
  }

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    switch (info.type) {
      case NVS_TYPE_U8: {
        uint8_t val;
        if (nvs_get_u8(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_I8: {
        int8_t val;
        if (nvs_get_i8(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_U16: {
        uint16_t val;
        if (nvs_get_u16(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_I16: {
        int16_t val;
        if (nvs_get_i16(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_U32: {
        uint32_t val;
        if (nvs_get_u32(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_I32: {
        int32_t val;
        if (nvs_get_i32(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, val);
        }
        break;
      }
      case NVS_TYPE_U64: {
        uint64_t val;
        if (nvs_get_u64(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, (double)val);
        }
        break;
      }
      case NVS_TYPE_I64: {
        int64_t val;
        if (nvs_get_i64(app_nvs_handle, info.key, &val) == ESP_OK) {
          cJSON_AddNumberToObject(root, info.key, (double)val);
        }
        break;
      }
      case NVS_TYPE_STR: {
        size_t required_size;
        if (nvs_get_str(app_nvs_handle, info.key, NULL, &required_size) == ESP_OK) {
          char* str_val = malloc(required_size);
          if (str_val) {
            if (nvs_get_str(app_nvs_handle, info.key, str_val, &required_size) == ESP_OK) {
              cJSON_AddStringToObject(root, info.key, str_val);
            }
            free(str_val);
          }
        }
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t required_size;
        if (nvs_get_blob(app_nvs_handle, info.key, NULL, &required_size) == ESP_OK) {
          uint8_t* blob_data = malloc(required_size);
          if (blob_data) {
            if (nvs_get_blob(app_nvs_handle, info.key, blob_data, &required_size) == ESP_OK) {
              // Base64 encode the blob
              size_t base64_len = 0;
              mbedtls_base64_encode(NULL, 0, &base64_len, blob_data, required_size);
              char* base64_str = malloc(base64_len + 1);
              if (base64_str) {
                size_t written;
                if (mbedtls_base64_encode((unsigned char*)base64_str, base64_len + 1,
                    &written, blob_data, required_size) == 0) {
                  base64_str[written] = '\0';
                  // Create blob wrapper object
                  cJSON* blob_obj = cJSON_CreateObject();
                  cJSON_AddStringToObject(blob_obj, "_blob", base64_str);
                  cJSON_AddItemToObject(root, info.key, blob_obj);
                }
                free(base64_str);
              }
            }
            free(blob_data);
          }
        }
        break;
      }
      default:
        break;
    }

    err = nvs_entry_next(&it);
    if (err != ESP_OK) break;
  }

  nvs_release_iterator(it);
  return root;
}

char* app_settings_export_json_string(bool pretty) {
  cJSON* json = app_settings_export_json();
  if (!json) return NULL;

  char* str = pretty ? cJSON_Print(json) : cJSON_PrintUnformatted(json);
  cJSON_Delete(json);
  return str;
}

int app_settings_import_json(const cJSON* json) {
  if (!json || !cJSON_IsObject(json)) {
    ESP_LOGE(TAG, "Invalid JSON object");
    return -1;
  }

  int count = 0;
  cJSON* item = NULL;

  cJSON_ArrayForEach(item, json) {
    const char* key = item->string;
    if (!key) continue;

    esp_err_t ret = ESP_FAIL;

    if (cJSON_IsNumber(item)) {
      // Store as u32 by default (most common case)
      double val = item->valuedouble;
      if (val >= 0 && val <= UINT32_MAX && val == (uint32_t)val) {
        ret = app_settings_save_u32(key, (uint32_t)val);
      } else if (val >= INT32_MIN && val <= INT32_MAX && val == (int32_t)val) {
        ret = nvs_set_i32(app_nvs_handle, key, (int32_t)val);
        if (ret == ESP_OK) nvs_commit(app_nvs_handle);
      } else {
        // Large number - store as i64
        ret = nvs_set_i64(app_nvs_handle, key, (int64_t)val);
        if (ret == ESP_OK) nvs_commit(app_nvs_handle);
      }
    } else if (cJSON_IsString(item)) {
      ret = app_settings_save_str(key, item->valuestring);
    } else if (cJSON_IsBool(item)) {
      ret = app_settings_save_u8(key, cJSON_IsTrue(item) ? 1 : 0);
    } else if (cJSON_IsObject(item)) {
      // Check for blob wrapper
      cJSON* blob_data = cJSON_GetObjectItem(item, "_blob");
      if (blob_data && cJSON_IsString(blob_data)) {
        const char* base64_str = blob_data->valuestring;
        size_t base64_len = strlen(base64_str);
        
        // Calculate decoded size
        size_t decoded_len = 0;
        mbedtls_base64_decode(NULL, 0, &decoded_len,
          (const unsigned char*)base64_str, base64_len);
        
        if (decoded_len > 0) {
          uint8_t* decoded = malloc(decoded_len);
          if (decoded) {
            size_t written;
            if (mbedtls_base64_decode(decoded, decoded_len, &written,
                (const unsigned char*)base64_str, base64_len) == 0) {
              ret = app_settings_save_blob(key, decoded, written);
            }
            free(decoded);
          }
        }
      }
    }

    if (ret == ESP_OK) {
      count++;
      ESP_LOGD(TAG, "Imported key: %s", key);
    } else {
      ESP_LOGW(TAG, "Failed to import key: %s", key);
    }
  }

  ESP_LOGI(TAG, "Imported %d settings", count);
  return count;
} 