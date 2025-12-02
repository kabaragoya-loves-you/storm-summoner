#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "esp_log.h"

#define TAG "KABARAGOYA"

extern lv_obj_t *canvas;

static lv_obj_t *g_screen = NULL;

static void kabaragoya_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    uint16_t disp_width = shared_canvas_buffer_get_width();
    uint16_t disp_height = shared_canvas_buffer_get_height();
    
    // Create a screen with image as background style (more efficient than widget)
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_width, disp_height);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_image_src(g_screen, "A:images/kabaragoya.bin", 0);
    lv_obj_set_style_bg_image_opa(g_screen, LV_OPA_COVER, 0);
    
    ESP_LOGI(TAG, "Kabaragoya screen created");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(kabaragoya, kabaragoya_draw_deferred_cb)

static void kabaragoya_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
  }
  ESP_LOGD(TAG, "Kabaragoya module teardown complete");
}

static void kabaragoya_init(void) {}

ui_draw_module_t kabaragoya_module = {
  .draw_func = kabaragoya_draw,
  .teardown_func = kabaragoya_teardown,
  .init_func = kabaragoya_init,
  .name = "kabaragoya"
};

