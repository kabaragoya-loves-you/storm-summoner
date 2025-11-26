#include "config.h"
#include "app_settings.h"
#include "esp_log.h"

static const char* TAG = "config";

#define NVS_KEY_PROGRAM_WRAP "prog_wrap"

// Cached settings
static bool s_program_wrap = true;  // Default: wrap around
static bool s_initialized = false;

esp_err_t config_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing config");
  
  // Load program_wrap from NVS
  bool wrap_val;
  if (app_settings_load_bool(NVS_KEY_PROGRAM_WRAP, &wrap_val) == ESP_OK) {
    s_program_wrap = wrap_val;
  }
  
  s_initialized = true;
  ESP_LOGI(TAG, "Config initialized: program_wrap=%s", s_program_wrap ? "on" : "off");
  
  return ESP_OK;
}

bool config_get_program_wrap(void) {
  return s_program_wrap;
}

esp_err_t config_set_program_wrap(bool wrap) {
  esp_err_t ret = app_settings_save_bool(NVS_KEY_PROGRAM_WRAP, wrap);
  if (ret == ESP_OK) {
    s_program_wrap = wrap;
    ESP_LOGI(TAG, "Program wrap set to %s", wrap ? "on" : "off");
  }
  return ret;
}

