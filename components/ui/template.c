#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"

#define TAG "TEMPLATE"

// External canvas reference
extern lv_obj_t *canvas;

// Module state variables (if needed)
// static float g_animation_value = 0.0f;
// static lv_timer_t *g_update_timer = NULL;

// Module configuration constants
// #define TEMPLATE_UPDATE_PERIOD_MS 50  // 20 FPS
// #define TEMPLATE_CENTER_X 64
// #define TEMPLATE_CENTER_Y 64

// Main drawing callback
static void template_draw_cb(lv_timer_t *timer) {
  if (!canvas) return;
  
  // Initialize drawing layer
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) {
    ESP_LOGE(TAG, "Failed to initialize canvas layer");
    return;
  }
  
  // Clear canvas with background color
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  // === YOUR DRAWING CODE HERE ===
  // Example: Draw a simple pixel
  // lv_canvas_set_px(canvas, 64, 64, lv_color_white(), LV_OPA_COVER);
  
  // Example: Draw using LVGL drawing descriptors
  // lv_draw_rect_dsc_t rect_dsc;
  // lv_draw_rect_dsc_init(&rect_dsc);
  // rect_dsc.bg_color = lv_color_make(128, 128, 128);
  // rect_dsc.bg_opa = LV_OPA_COVER;
  // rect_dsc.radius = 5;
  // lv_area_t area = {30, 30, 98, 98};
  // lv_draw_rect(&layer, &rect_dsc, &area);
  
  // Finish drawing and update display
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}

// Deferred draw function - called once after module is selected
static void template_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  // Create update timer if continuous animation is needed
  // Uncomment the following lines for animated content:
  /*
  if (!g_update_timer) {
    g_update_timer = lv_timer_create(template_draw_cb, TEMPLATE_UPDATE_PERIOD_MS, NULL);
    lv_timer_set_repeat_count(g_update_timer, -1);
  }
  */
  
  // Draw the first frame immediately
  template_draw_cb(NULL);
  
  // Delete the one-shot timer
  lv_timer_del(timer);
}

// Use the UI macro to create the draw function
UI_CREATE_DEFERRED_DRAW_FUNC(template, template_draw_deferred_cb)

// Module teardown - clean up resources when switching away
static void template_teardown(void) {
  // Clean up any timers
  /*
  if (g_update_timer) {
    lv_timer_del(g_update_timer);
    g_update_timer = NULL;
  }
  */
  
  // Clean up any allocated resources
  // Example: free(g_dynamic_data);
  
  ESP_LOGD(TAG, "Template module teardown complete");
}

// Module initialization - set up initial state
static void template_init(void) {
  // Initialize module state
  // g_animation_value = 0.0f;
  
  // Allocate any needed resources
  // Example: g_dynamic_data = malloc(sizeof(my_data_t));
  
  ESP_LOGI(TAG, "Template module initialized");
}

// Module definition - register with UI system
ui_draw_module_t template_module = {
  .draw_func = template_draw,
  .teardown_func = template_teardown,
  .init_func = template_init,
  .name = "template"
};
