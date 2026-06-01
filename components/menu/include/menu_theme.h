#ifndef MENU_THEME_H
#define MENU_THEME_H

#include "lvgl.h"
#include "esp_err.h"

typedef enum {
  MENU_THEME_DEFAULT = 0,
  MENU_THEME_MONOTONE,
  MENU_THEME_CVD,
} menu_theme_t;

#define MENU_THEME_COUNT 3

typedef struct {
  lv_color_t item_auto;
  lv_color_t item_submenu;
  lv_color_t item_roller;
  lv_color_t item_action;
  lv_color_t item_display;
  lv_color_t title_bar_bg;
  lv_color_t title_bar_grad;
  lv_color_t title_bar_grad_2line;
  lv_color_t title_text;
  lv_color_t divider;
  lv_color_t roller_text;
  lv_color_t roller_selected_bg;
  lv_color_t roller_selected_text;
} menu_theme_palette_t;

esp_err_t menu_theme_init(void);
menu_theme_t menu_theme_get(void);
esp_err_t menu_theme_set(menu_theme_t theme);
const char* menu_theme_to_string(menu_theme_t theme);
const menu_theme_palette_t* menu_theme_get_palette(void);

#endif /* MENU_THEME_H */
