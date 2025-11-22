#include "lvgl.h"
#include "ui.h"
#include "lv_lizard.h"
#include "esp_log.h"

#define TAG "DRAW_LIZARD"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_lizard = NULL;

static void draw_lizard_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  // Only create widgets if they don't exist
  if (g_screen == NULL) {
    // Get the display from canvas
    lv_display_t *disp = lv_obj_get_display(canvas);
    if (!disp) {
      ESP_LOGE(TAG, "Failed to get display from canvas");
      lv_timer_delete(timer);
      return;
    }
    
    // Create a screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, 128, 128);
    
    // Set black background
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    
    // Create lizard widget
    g_lizard = lv_lizard_create(g_screen);
    lv_obj_set_size(g_lizard, 128, 128);
    lv_obj_align(g_lizard, LV_ALIGN_CENTER, 0, 0);
    
    // Configure to match original (no rotation, full opacity, centered pivot)
    lv_lizard_set_angle(g_lizard, 0);
    lv_lizard_set_scale(g_lizard, 256);  // 1.0 scale
    lv_lizard_set_opa(g_lizard, LV_OPA_COVER);
    lv_lizard_set_pivot(g_lizard, 64, 64);  // Center pivot
    
    ESP_LOGI(TAG, "Lizard screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(draw_lizard, draw_lizard_draw_deferred_cb)

static void draw_lizard_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_lizard = NULL;
  }
  ESP_LOGD(TAG, "Lizard module teardown complete");
}

static void draw_lizard_init(void) {}

ui_draw_module_t draw_lizard_module = {
  .draw_func = draw_lizard_draw,
  .teardown_func = draw_lizard_teardown,
  .init_func = draw_lizard_init,
  .name = "draw_lizard"
};
