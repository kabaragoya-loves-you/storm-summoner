#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"
#include "display_driver.h"

#define TAG "WORKING_UI"

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_label = NULL;
static const char *g_message = "Working...";

// Deferred drawing callback
static void working_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen == NULL) {
    uint16_t disp_w = display_get_width();
    uint16_t disp_h = display_get_height();
    
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_w, disp_h);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_screen, 0, 0);
    
    g_label = lv_label_create(g_screen);
    lv_label_set_text(g_label, g_message);
    lv_obj_set_style_text_color(g_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(g_label, &lv_font_montserrat_14, 0);
    lv_obj_align(g_label, LV_ALIGN_CENTER, 0, 0);
    
    ESP_LOGI(TAG, "Working screen created: %s", g_message);
  }
  
  // Update label text if changed
  if (g_label) {
    lv_label_set_text(g_label, g_message);
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(working, working_draw_deferred_cb)

static void working_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_label = NULL;
  }
  ESP_LOGD(TAG, "Working module teardown");
}

static void working_init(void) {
  ESP_LOGI(TAG, "Working module initialized");
}

// Set the message to display
void working_set_message(const char *msg) {
  g_message = msg ? msg : "Working...";
  if (g_label) {
    lv_label_set_text(g_label, g_message);
  }
}

ui_draw_module_t working_module = {
  .draw_func = working_draw,
  .teardown_func = working_teardown,
  .init_func = working_init,
  .name = "working"
};
