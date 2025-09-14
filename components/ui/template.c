#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"

#define TAG "TEMPLATE"

extern lv_obj_t *canvas;

// Widget references (static to persist across frames)
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_label = NULL;

// Deferred drawing callback - called after LVGL is ready
static void template_draw_deferred_cb(lv_timer_t *timer) {
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
  
  // Set background style
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Create a simple label widget
  g_label = lv_label_create(g_screen);
  lv_label_set_text(g_label, "Template UI");
  lv_obj_set_style_text_color(g_label, lv_color_white(), 0);
  lv_obj_align(g_label, LV_ALIGN_CENTER, 0, 0);
  
  // Load the screen
  lv_screen_load(g_screen);
  
  ESP_LOGI(TAG, "Template screen created");
  
  // Clean up the timer
  lv_timer_del(timer);
}

// Macro creates the draw function that schedules deferred drawing
UI_CREATE_DEFERRED_DRAW_FUNC(template, template_draw_deferred_cb)

// Teardown function - clean up resources
static void template_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_label = NULL;  // Child widgets are automatically deleted
  }
  ESP_LOGD(TAG, "Template module teardown complete");
}

// Init function - called once when module is registered
static void template_init(void) {
  ESP_LOGI(TAG, "Template module initialized");
}

// Module definition
ui_draw_module_t template_module = {
  .draw_func = template_draw,
  .teardown_func = template_teardown,
  .init_func = template_init,
  .name = "template"
};
