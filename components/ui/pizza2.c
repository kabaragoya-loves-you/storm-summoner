#include "lvgl.h"
#include "ui.h"
#include "lv_pizza2.h"
#include "esp_log.h"

#define TAG "PIZZA2"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_pizza2 = NULL;

static void pizza2_draw_deferred_cb(lv_timer_t *timer) {
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
    
    // Create pizza2 widget
    g_pizza2 = lv_pizza2_create(g_screen);
    lv_obj_set_size(g_pizza2, 128, 128);
    lv_obj_align(g_pizza2, LV_ALIGN_CENTER, 0, 0);
    
    // Configure to match original
    lv_pizza2_set_slice_count(g_pizza2, 8);
    lv_pizza2_set_gray_tone(g_pizza2, 6);
    lv_pizza2_set_radius(g_pizza2, 60);
    lv_pizza2_set_bite_size(g_pizza2, 25);
    
    // Set alternating pattern (0xAA = 10101010 binary)
    lv_pizza2_set_active_slices(g_pizza2, 0xAA);
    
    ESP_LOGI(TAG, "Pizza2 screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(pizza2, pizza2_draw_deferred_cb)

static void pizza2_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_pizza2 = NULL;
  }
  ESP_LOGD(TAG, "Pizza2 module teardown complete");
}

static void pizza2_init(void) {
  // No init needed
}

ui_draw_module_t pizza2_module = {
  .draw_func = pizza2_draw,
  .teardown_func = pizza2_teardown,
  .init_func = pizza2_init,
  .name = "pizza2"
};
