#include "lvgl.h"
#include "ui.h"
#include "lv_boundary_circle.h"
#include "esp_log.h"

#define TAG "BOUNDARY_CIRCLE"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_boundary = NULL;

static void boundary_circle_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  // Only create widgets if they don't exist
  if (g_screen == NULL) {
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
    
    // Create boundary circle widget
    g_boundary = lv_boundary_circle_create(g_screen);
    lv_obj_set_size(g_boundary, 128, 128);
    lv_obj_align(g_boundary, LV_ALIGN_CENTER, 0, 0);
    
    // Configure to match original (white, 1px width, margin of 1 to get radius 63)
    lv_boundary_circle_set_color(g_boundary, lv_color_white());
    lv_boundary_circle_set_width(g_boundary, 1);
    lv_boundary_circle_set_margin(g_boundary, 1);
    
    ESP_LOGI(TAG, "Boundary circle screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(boundary_circle, boundary_circle_draw_deferred_cb)

static void boundary_circle_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_boundary = NULL;
  }
  ESP_LOGD(TAG, "Boundary circle module teardown complete");
}

static void boundary_circle_init(void) {}

ui_draw_module_t boundary_circle_module = {
  .draw_func = boundary_circle_draw,
  .teardown_func = boundary_circle_teardown,
  .init_func = boundary_circle_init,
  .name = "boundary_circle"
};
