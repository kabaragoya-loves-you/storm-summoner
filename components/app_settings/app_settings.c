#include "app_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

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

esp_err_t app_settings_save_u16(const char* key, uint16_t value) {
    return nvs_set_u16(app_nvs_handle, key, value);
}

esp_err_t app_settings_load_u16(const char* key, uint16_t* value) {
    return nvs_get_u16(app_nvs_handle, key, value);
}

esp_err_t app_settings_save_u32(const char* key, uint32_t value) {
    return nvs_set_u32(app_nvs_handle, key, value);
}

esp_err_t app_settings_load_u32(const char* key, uint32_t* value) {
    return nvs_get_u32(app_nvs_handle, key, value);
}

esp_err_t app_settings_save_bool(const char* key, bool value) {
    return nvs_set_u8(app_nvs_handle, key, value ? 1 : 0);
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
    return nvs_set_str(app_nvs_handle, key, value);
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
    return nvs_erase_key(app_nvs_handle, key);
}

esp_err_t app_settings_erase_all(void) {
    return nvs_erase_all(app_nvs_handle);
} 