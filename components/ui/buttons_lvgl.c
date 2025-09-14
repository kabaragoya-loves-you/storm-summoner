#include "lvgl.h"
#include "ui.h"
#include "lv_radar.h"
#include "lv_slices.h"
#include "lv_globe.h"
#include "esp_log.h"

#define TAG "BUTTONS_LVGL"
#define BUTTONS_CENTER_X 64
#define BUTTONS_CENTER_Y 64

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_radar = NULL;
static lv_obj_t *g_slices = NULL;
static lv_obj_t *g_globe = NULL;

static void buttons_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  // Get the display from canvas
  lv_display_t *disp = lv_obj_get_display(canvas);
  if (!disp) {
    ESP_LOGE(TAG, "Failed to get display from canvas");
    lv_timer_del(timer);
    return;
  }
  
  // Create a screen
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, 128, 128);
  
  // Set black background
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Create widgets in order (bottom to top)
  
  // Create radar widget (bottom layer)
  g_radar = lv_radar_create(g_screen);
  lv_obj_set_size(g_radar, 128, 128);
  lv_obj_align(g_radar, LV_ALIGN_CENTER, 0, 0);
  
  // Configure radar
  lv_radar_set_line_count(g_radar, 8);
  lv_radar_set_radius_range(g_radar, 0, 64);
  lv_radar_set_dot_pattern(g_radar, 8, 1);  // 8 pixel gaps, 1 pixel dots
  lv_radar_set_line_style(g_radar, lv_color_make(17, 17, 17), LV_OPA_COVER);
  
  // Create slices widget
  g_slices = lv_slices_create(g_screen);
  lv_obj_set_size(g_slices, 128, 128);
  lv_obj_align(g_slices, LV_ALIGN_CENTER, 0, 0);
  
  // Configure slices
  lv_slices_set_count(g_slices, 8);
  lv_slices_set_radius(g_slices, 25, 60);
  lv_slices_set_colors(g_slices, lv_color_make(102, 102, 102), lv_color_black());
  lv_slices_set_opacity(g_slices, LV_OPA_COVER, LV_OPA_TRANSP);
  // Use default state callback which checks touch input
  
  // Create globe widget (center, on top)
  g_globe = lv_globe_create(g_screen);
  lv_obj_align(g_globe, LV_ALIGN_CENTER, 0, 0);
  
  // Configure globe (matching original buttons.c parameters)
  lv_globe_set_radius(g_globe, 25);  // Original BUTTONS_GLOBE_RADIUS
  lv_globe_set_scale(g_globe, 0.8f);  // Original BUTTONS_GLOBE_SCALE
  lv_globe_set_rotation_speed(g_globe, 0, 0.02f, 0);  // Slow Y-axis rotation
  lv_globe_set_auto_rotate(g_globe, true);
  
  // Load the screen
  lv_screen_load(g_screen);
  
  ESP_LOGI(TAG, "LVGL buttons screen created with radar widget");
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons_lvgl, buttons_draw_deferred_cb)

static void buttons_lvgl_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_radar = NULL;
    g_slices = NULL;
  }
  ESP_LOGD(TAG, "Buttons LVGL module teardown complete");
}

static void buttons_lvgl_init(void) {
  ESP_LOGI(TAG, "Buttons LVGL module initialized");
}

ui_draw_module_t buttons_lvgl_module = {
  .draw_func = buttons_lvgl_draw,
  .teardown_func = buttons_lvgl_teardown,
  .init_func = buttons_lvgl_init,
  .name = "buttons_lvgl"
};
