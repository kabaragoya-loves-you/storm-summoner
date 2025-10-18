#include "lvgl.h"
#include "ui.h"
#include "lv_pizza.h"
#include "esp_log.h"

#define TAG "PIZZA"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_pizza = NULL;

static void pizza_draw_deferred_cb(lv_timer_t *timer) {
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
    
    // Create pizza widget
    g_pizza = lv_pizza_create(g_screen);
    lv_obj_set_size(g_pizza, 128, 128);
    lv_obj_align(g_pizza, LV_ALIGN_CENTER, 0, 0);
    
    // Configure to match original (8 slices, white, 1px width, margin of 4 to get radius 60)
    lv_pizza_set_slice_count(g_pizza, 8);  // 8 lines = 4 crossing lines
    lv_pizza_set_color(g_pizza, lv_color_white());
    lv_pizza_set_width(g_pizza, 1);
    lv_pizza_set_margin(g_pizza, 4);  // 64 - 60 = 4
    lv_pizza_set_circle_enabled(g_pizza, true);
    
    ESP_LOGI(TAG, "Pizza screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_del(timer);
}

// Use the macro to create the deferred draw function
UI_CREATE_DEFERRED_DRAW_FUNC(pizza, pizza_draw_deferred_cb)

static void pizza_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_pizza = NULL;
  }
  ESP_LOGD(TAG, "Pizza module teardown complete");
}

static void pizza_init(void) {}

ui_draw_module_t pizza_module = {
  .draw_func = pizza_draw,
  .teardown_func = pizza_teardown,
  .init_func = pizza_init,
  .name = "pizza"
};
