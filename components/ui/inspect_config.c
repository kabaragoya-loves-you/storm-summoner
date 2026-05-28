#include "inspect_config.h"
#include "app_settings.h"
#include "esp_log.h"

static const char *TAG = "inspect_config";

#define NVS_KEY_SCROLL_SPEED "inspect_scroll_spd"
#define NVS_KEY_SCROLL_MODE  "inspect_scroll_mode"

static inspect_scroll_speed_t s_scroll_speed = INSPECT_SCROLL_SPEED_MEDIUM;
static inspect_scroll_mode_t s_scroll_mode = INSPECT_SCROLL_MODE_PING_PONG;

static const int32_t s_speed_steps[INSPECT_SCROLL_SPEED_MAX] = {3, 12, 24};

void inspect_config_init(void) {
  uint8_t speed = (uint8_t)INSPECT_SCROLL_SPEED_MEDIUM;
  if (app_settings_load_u8(NVS_KEY_SCROLL_SPEED, &speed) == APP_SETTINGS_OK &&
      speed < INSPECT_SCROLL_SPEED_MAX) {
    s_scroll_speed = (inspect_scroll_speed_t)speed;
  }

  uint8_t mode = (uint8_t)INSPECT_SCROLL_MODE_PING_PONG;
  if (app_settings_load_u8(NVS_KEY_SCROLL_MODE, &mode) == APP_SETTINGS_OK &&
      mode < INSPECT_SCROLL_MODE_MAX) {
    s_scroll_mode = (inspect_scroll_mode_t)mode;
  }

  ESP_LOGI(TAG, "Scroll speed=%s mode=%s",
    inspect_config_scroll_speed_name(s_scroll_speed),
    inspect_config_scroll_mode_name(s_scroll_mode));
}

inspect_scroll_speed_t inspect_config_get_scroll_speed(void) {
  return s_scroll_speed;
}

esp_err_t inspect_config_set_scroll_speed(inspect_scroll_speed_t speed) {
  if (speed >= INSPECT_SCROLL_SPEED_MAX) return ESP_ERR_INVALID_ARG;
  s_scroll_speed = speed;
  return app_settings_save_u8(NVS_KEY_SCROLL_SPEED, (uint8_t)speed);
}

inspect_scroll_mode_t inspect_config_get_scroll_mode(void) {
  return s_scroll_mode;
}

esp_err_t inspect_config_set_scroll_mode(inspect_scroll_mode_t mode) {
  if (mode >= INSPECT_SCROLL_MODE_MAX) return ESP_ERR_INVALID_ARG;
  s_scroll_mode = mode;
  return app_settings_save_u8(NVS_KEY_SCROLL_MODE, (uint8_t)mode);
}

int32_t inspect_config_scroll_step_px(void) {
  return s_speed_steps[s_scroll_speed];
}

const char *inspect_config_scroll_speed_name(inspect_scroll_speed_t speed) {
  switch (speed) {
    case INSPECT_SCROLL_SPEED_SLOW: return "Slow";
    case INSPECT_SCROLL_SPEED_MEDIUM: return "Medium";
    case INSPECT_SCROLL_SPEED_FAST: return "Fast";
    default: return "Medium";
  }
}

const char *inspect_config_scroll_mode_name(inspect_scroll_mode_t mode) {
  switch (mode) {
    case INSPECT_SCROLL_MODE_PING_PONG: return "Ping-Pong";
    case INSPECT_SCROLL_MODE_LOOP_DOWN: return "Loop Down";
    default: return "Ping-Pong";
  }
}
