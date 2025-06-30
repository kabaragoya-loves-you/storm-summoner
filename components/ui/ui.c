#include "lvgl.h"
#include "../lvgl/src/display/lv_display_private.h"
#include "ui.h"
#include "esp_log.h"
#include <string.h>

lv_obj_t *canvas = NULL;
static lv_timer_t *g_ui_refresh_timer = NULL;
static ui_draw_module_t* current_draw_module = NULL;

static lv_color_t display_buf[128 * 128] __attribute__((aligned(4)));

#define TAG "UI"

app_mode_t g_app_mode = APP_MODE_PERFORMANCE;
bool g_at_programming_top_level_menu = false;

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  // Only invalidate if not currently rendering to prevent feedback loops
  if (canvas != NULL) {
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
  }
}

void ui_set_draw_module(ui_draw_module_t* module) {
  if (!module) {
    ESP_LOGW(TAG, "Attempted to set NULL module");
    return;
  }

  if (current_draw_module && current_draw_module->teardown_func) current_draw_module->teardown_func();

  if (canvas) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (layer.draw_buf) {
      lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
      lv_canvas_finish_layer(canvas, &layer);
      lv_obj_invalidate(canvas);
    }
  }

  current_draw_module = module;
  ESP_LOGI(TAG, "Switched to module: %s", module->name);

  if (module->init_func) module->init_func();

  if (module->draw_func) module->draw_func();
}

ui_draw_module_t* ui_get_current_module(void) {
  return current_draw_module;
}

static void deferred_canvas_hide_cb(lv_timer_t *timer) {
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_timer_del(timer);
}

static void deferred_canvas_show_cb(lv_timer_t *timer) {
  if (canvas != NULL) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    // Only invalidate if not currently rendering to prevent feedback loops
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
  }
  lv_timer_del(timer);
}



void ui_init(void) {
  ESP_LOGI(TAG, "Mode 5: Clean RGB565 with I4 display driver conversion (double buffering)");

  // Standard RGB565 canvas - same as all other modes
  canvas = lv_canvas_create(lv_scr_act());
  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);
  
  memset(display_buf, 0, sizeof(display_buf));
  lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_RGB565);
  ESP_LOGI(TAG, "Canvas created: %d bytes", (int)sizeof(display_buf));

  g_ui_refresh_timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps

  g_app_mode = APP_MODE_PERFORMANCE;
  g_at_programming_top_level_menu = false;
}

app_mode_t ui_get_app_mode(void) {
  return g_app_mode;
}

void ui_set_app_mode(app_mode_t mode) {
  app_mode_t previous_mode = g_app_mode;
  g_app_mode = mode;
  
  const char* mode_names[] = {"Performance", "Programming", "Screensaver"};
  const char* prev_name = (previous_mode < 3) ? mode_names[previous_mode] : "Unknown";
  const char* new_name = (mode < 3) ? mode_names[mode] : "Unknown";
  
  ESP_LOGI(TAG, "App mode changed: %s -> %s", prev_name, new_name);
}

bool ui_is_programming_top_level(void) {
  return g_at_programming_top_level_menu;
}

void ui_set_programming_top_level(bool is_top_level) {
  g_at_programming_top_level_menu = is_top_level;
  ESP_LOGI(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
}

void ui_graphics_suspend(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);

  // Defer canvas hiding to avoid blocking in timer callback context
  lv_timer_t *hide_timer = lv_timer_create(deferred_canvas_hide_cb, 1, NULL);
  if (hide_timer != NULL) lv_timer_set_repeat_count(hide_timer, 1);
}

void ui_graphics_resume(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);

  // Defer canvas showing to avoid blocking in timer callback context
  lv_timer_t *show_timer = lv_timer_create(deferred_canvas_show_cb, 1, NULL);
  if (show_timer != NULL) lv_timer_set_repeat_count(show_timer, 1);
}
