#include "lvgl.h"
#include "ui.h"
#include "lv_pizza2.h"
#include "esp_log.h"

#define TAG "SLICES"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_slices_widget = NULL;

static void slices_draw_deferred_cb(lv_timer_t *timer) {
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
    
    // Create slices widget (uses lv_pizza2 internally)
    g_slices_widget = lv_pizza2_create(g_screen);
    lv_obj_set_size(g_slices_widget, 128, 128);
    lv_obj_align(g_slices_widget, LV_ALIGN_CENTER, 0, 0);
    
    // Configure slices
    lv_pizza2_set_slice_count(g_slices_widget, 8);
    lv_pizza2_set_gray_tone(g_slices_widget, 6);
    lv_pizza2_set_radius(g_slices_widget, 60);
    lv_pizza2_set_bite_size(g_slices_widget, 25);
    
    // Set alternating pattern (0xAA = 10101010 binary)
    lv_pizza2_set_active_slices(g_slices_widget, 0xAA);
    
    ESP_LOGI(TAG, "Slices screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(slices, slices_draw_deferred_cb)

static void slices_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_slices_widget = NULL;
  }
  ESP_LOGD(TAG, "Slices teardown complete");
}

static void slices_init(void) {
  // No init needed
}

ui_draw_module_t slices_module = {
  .draw_func = slices_draw,
  .teardown_func = slices_teardown,
  .init_func = slices_init,
  .name = "slices"
};
