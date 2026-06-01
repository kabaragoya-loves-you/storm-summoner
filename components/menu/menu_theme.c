#include "menu_theme.h"
#include "app_settings.h"
#include "esp_log.h"

#define TAG "MENU_THEME"
#define NVS_KEY_MENU_THEME "menu_theme"

static menu_theme_t s_theme = MENU_THEME_DEFAULT;
static bool s_initialized = false;

static const menu_theme_palette_t s_default_palette = {
  .item_auto = LV_COLOR_MAKE(255, 255, 255),
  .item_submenu = LV_COLOR_MAKE(90, 160, 255),
  .item_roller = LV_COLOR_MAKE(255, 150, 40),
  .item_action = LV_COLOR_MAKE(100, 200, 110),
  .item_display = LV_COLOR_MAKE(160, 160, 160),
  .title_bar_bg = LV_COLOR_MAKE(101, 67, 33),
  .title_bar_grad = LV_COLOR_MAKE(139, 90, 43),
  .title_bar_grad_2line = LV_COLOR_MAKE(60, 40, 20),
  .title_text = LV_COLOR_MAKE(255, 248, 220),
  .divider = LV_COLOR_MAKE(80, 80, 80),
  .roller_text = LV_COLOR_MAKE(160, 160, 160),
  .roller_selected_bg = LV_COLOR_MAKE(60, 60, 60),
  .roller_selected_text = LV_COLOR_MAKE(255, 255, 255),
};

static const menu_theme_palette_t s_monotone_palette = {
  .item_auto = LV_COLOR_MAKE(255, 255, 255),
  .item_submenu = LV_COLOR_MAKE(255, 255, 255),
  .item_roller = LV_COLOR_MAKE(255, 255, 255),
  .item_action = LV_COLOR_MAKE(255, 255, 255),
  .item_display = LV_COLOR_MAKE(160, 160, 160),
  .title_bar_bg = LV_COLOR_MAKE(101, 67, 33),
  .title_bar_grad = LV_COLOR_MAKE(139, 90, 43),
  .title_bar_grad_2line = LV_COLOR_MAKE(60, 40, 20),
  .title_text = LV_COLOR_MAKE(255, 248, 220),
  .divider = LV_COLOR_MAKE(80, 80, 80),
  .roller_text = LV_COLOR_MAKE(160, 160, 160),
  .roller_selected_bg = LV_COLOR_MAKE(60, 60, 60),
  .roller_selected_text = LV_COLOR_MAKE(255, 255, 255),
};

static const menu_theme_palette_t s_cvd_palette = {
  .item_auto = LV_COLOR_MAKE(255, 255, 255),
  .item_submenu = LV_COLOR_MAKE(86, 180, 233),
  .item_roller = LV_COLOR_MAKE(230, 159, 0),
  .item_action = LV_COLOR_MAKE(0, 158, 115),
  .item_display = LV_COLOR_MAKE(200, 200, 200),
  .title_bar_bg = LV_COLOR_MAKE(40, 40, 40),
  .title_bar_grad = LV_COLOR_MAKE(70, 70, 70),
  .title_bar_grad_2line = LV_COLOR_MAKE(70, 70, 70),
  .title_text = LV_COLOR_MAKE(255, 255, 255),
  .divider = LV_COLOR_MAKE(120, 120, 120),
  .roller_text = LV_COLOR_MAKE(180, 180, 180),
  .roller_selected_bg = LV_COLOR_MAKE(90, 90, 90),
  .roller_selected_text = LV_COLOR_MAKE(255, 255, 255),
};

static const menu_theme_palette_t* palette_for_theme(menu_theme_t theme) {
  switch (theme) {
    case MENU_THEME_MONOTONE: return &s_monotone_palette;
    case MENU_THEME_CVD: return &s_cvd_palette;
    default: return &s_default_palette;
  }
}

esp_err_t menu_theme_init(void) {
  if (s_initialized) return ESP_OK;

  uint8_t stored = MENU_THEME_DEFAULT;
  if (app_settings_load_u8(NVS_KEY_MENU_THEME, &stored) == ESP_OK &&
      stored < MENU_THEME_COUNT) {
    s_theme = (menu_theme_t)stored;
  } else {
    s_theme = MENU_THEME_DEFAULT;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Menu theme: %s", menu_theme_to_string(s_theme));
  return ESP_OK;
}

menu_theme_t menu_theme_get(void) {
  return s_theme;
}

esp_err_t menu_theme_set(menu_theme_t theme) {
  if (theme >= MENU_THEME_COUNT) return ESP_ERR_INVALID_ARG;

  s_theme = theme;
  if (!s_initialized) s_initialized = true;

  esp_err_t ret = app_settings_save_u8(NVS_KEY_MENU_THEME, (uint8_t)theme);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Menu theme set to %s", menu_theme_to_string(theme));
  }
  return ret;
}

const char* menu_theme_to_string(menu_theme_t theme) {
  switch (theme) {
    case MENU_THEME_MONOTONE: return "Monotone";
    case MENU_THEME_CVD: return "CVD";
    default: return "Default";
  }
}

const menu_theme_palette_t* menu_theme_get_palette(void) {
  return palette_for_theme(s_theme);
}
