#include "config.h"
#include "app_settings.h"
#include "esp_log.h"

static const char* TAG = "config";

#define NVS_KEY_PROGRAM_WRAP "prog_wrap"
#define NVS_KEY_PERSIST_SCENE "persist_scn"
#define NVS_KEY_LAST_SCENE "last_scene"

// Cached settings
static bool s_preset_wrap = false;   // Default: clamp at boundaries
static bool s_persist_scene = false; // Default: always boot to scene 1
static uint8_t s_last_scene = 0;     // Default: scene index 0
static bool s_initialized = false;

esp_err_t config_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing config");
  
  // Load preset_wrap from NVS
  bool wrap_val;
  if (app_settings_load_bool(NVS_KEY_PROGRAM_WRAP, &wrap_val) == ESP_OK) {
    s_preset_wrap = wrap_val;
  }
  
  // Load persist_scene from NVS
  bool persist_val;
  if (app_settings_load_bool(NVS_KEY_PERSIST_SCENE, &persist_val) == ESP_OK) {
    s_persist_scene = persist_val;
  }
  
  // Load last_scene from NVS
  uint8_t scene_val;
  if (app_settings_load_u8(NVS_KEY_LAST_SCENE, &scene_val) == ESP_OK) {
    s_last_scene = scene_val;
  }
  
  s_initialized = true;
  ESP_LOGI(TAG, "Config initialized: preset_wrap=%s, persist_scene=%s, last_scene=%d",
    s_preset_wrap ? "on" : "off",
    s_persist_scene ? "on" : "off",
    s_last_scene);
  
  return ESP_OK;
}

bool config_get_preset_wrap(void) {
  return s_preset_wrap;
}

esp_err_t config_set_preset_wrap(bool wrap) {
  esp_err_t ret = app_settings_save_bool(NVS_KEY_PROGRAM_WRAP, wrap);
  if (ret == ESP_OK) {
    s_preset_wrap = wrap;
    ESP_LOGI(TAG, "Preset wrap set to %s", wrap ? "on" : "off");
  }
  return ret;
}

bool config_get_persist_scene(void) {
  return s_persist_scene;
}

esp_err_t config_set_persist_scene(bool persist) {
  esp_err_t ret = app_settings_save_bool(NVS_KEY_PERSIST_SCENE, persist);
  if (ret == ESP_OK) {
    s_persist_scene = persist;
    ESP_LOGI(TAG, "Persist scene set to %s", persist ? "on" : "off");
  }
  return ret;
}

uint8_t config_get_last_scene(void) {
  return s_last_scene;
}

esp_err_t config_set_last_scene(uint8_t scene_index) {
  esp_err_t ret = app_settings_save_u8(NVS_KEY_LAST_SCENE, scene_index);
  if (ret == ESP_OK) {
    s_last_scene = scene_index;
  }
  return ret;
}

