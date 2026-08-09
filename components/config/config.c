#include "config.h"
#include "app_settings.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "config";

#define NVS_KEY_PROGRAM_WRAP "prog_wrap"
#define NVS_KEY_PERSIST_SCENE "persist_scn"
#define NVS_KEY_LAST_SCENE "last_scene"
#define NVS_KEY_DEVICE_MODE "dev_mode"
#define NVS_KEY_CC_MIRROR "cc_mirror"
#define NVS_KEY_USER_HANDLE "user_handle"

// Cached settings
static bool s_preset_wrap = false;   // Default: clamp at boundaries
static bool s_persist_scene = false; // Default: always boot to scene 1
static uint8_t s_last_scene = 0;     // Default: scene index 0
static device_mode_t s_device_mode = DEVICE_MODE_SINGLE; // Default: single device for all scenes
static bool s_cc_mirror = false;     // Default: ignore incoming CC for mode tracking
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
  
  // Load device_mode from NVS
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_DEVICE_MODE, &mode_val) == ESP_OK) {
    s_device_mode = (mode_val == 1) ? DEVICE_MODE_PER_SCENE : DEVICE_MODE_SINGLE;
  }
  
  // Load cc_mirror from NVS
  bool cc_mirror_val;
  if (app_settings_load_bool(NVS_KEY_CC_MIRROR, &cc_mirror_val) == ESP_OK) {
    s_cc_mirror = cc_mirror_val;
  }
  
  s_initialized = true;
  ESP_LOGI(TAG, "Config initialized: preset_wrap=%s, persist_scene=%s, last_scene=%d, "
    "device_mode=%s, cc_mirror=%s",
    s_preset_wrap ? "on" : "off",
    s_persist_scene ? "on" : "off",
    s_last_scene,
    s_device_mode == DEVICE_MODE_PER_SCENE ? "per_scene" : "single",
    s_cc_mirror ? "on" : "off");
  
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

device_mode_t config_get_device_mode(void) {
  return s_device_mode;
}

esp_err_t config_set_device_mode(device_mode_t mode) {
  esp_err_t ret = app_settings_save_u8(NVS_KEY_DEVICE_MODE, (uint8_t)mode);
  if (ret == ESP_OK) {
    s_device_mode = mode;
    ESP_LOGI(TAG, "Device mode set to %s",
      mode == DEVICE_MODE_PER_SCENE ? "per_scene" : "single");
  }
  return ret;
}

bool config_get_cc_mirror(void) {
  return s_cc_mirror;
}

esp_err_t config_set_cc_mirror(bool enabled) {
  esp_err_t ret = app_settings_save_bool(NVS_KEY_CC_MIRROR, enabled);
  if (ret == ESP_OK) {
    s_cc_mirror = enabled;
    ESP_LOGI(TAG, "Incoming CC mirror set to %s", enabled ? "on" : "off");
  }
  return ret;
}

static void sanitize_user_handle(const char* input, char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  out[0] = '\0';
  if (!input) return;

  size_t pos = 0;
  for (size_t i = 0; input[i] != '\0' && pos < USER_HANDLE_MAX_LEN; i++) {
    char c = input[i];
    if (c == ' ') c = '-';
    if (pos < out_size - 1) out[pos++] = c;
  }
  out[pos] = '\0';

  while (pos > 0 && out[pos - 1] == '-') out[--pos] = '\0';

  size_t start = 0;
  while (out[start] == '-') start++;
  if (start > 0) memmove(out, out + start, pos - start + 1);
}

esp_err_t config_get_user_handle(char* buf, size_t len) {
  if (!buf || len == 0) return ESP_ERR_INVALID_ARG;
  buf[0] = '\0';

  esp_err_t ret = app_settings_load_str(NVS_KEY_USER_HANDLE, buf, len);
  if (ret != ESP_OK) return ret;
  return ESP_OK;
}

esp_err_t config_set_user_handle(const char* handle) {
  if (!handle) return ESP_ERR_INVALID_ARG;

  char sanitized[USER_HANDLE_MAX_LEN + 1];
  sanitize_user_handle(handle, sanitized, sizeof(sanitized));
  if (sanitized[0] == '\0') return ESP_ERR_INVALID_ARG;

  esp_err_t ret = app_settings_save_str(NVS_KEY_USER_HANDLE, sanitized);
  if (ret == ESP_OK) ESP_LOGI(TAG, "User handle set to '%s'", sanitized);
  return ret;
}

