#include "lvgl.h"
#include "../lvgl/src/display/lv_display_private.h"
#include "ui.h"
#include "esp_log.h"
#include <string.h>

void ui_event_handler_init(void);

lv_obj_t *canvas = NULL;
static lv_timer_t *g_ui_refresh_timer = NULL;
static ui_draw_module_t* current_draw_module = NULL;

static void *display_buf = NULL;
static bool g_teardown_in_progress = false;

#define TAG "UI"

app_mode_t g_app_mode = APP_MODE_PERFORMANCE;
bool g_at_programming_top_level_menu = false;

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  // Only invalidate if not currently rendering to prevent feedback loops
  if (canvas != NULL) {
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
  }
}

void ui_set_draw_module(ui_draw_module_t* module) {
  if (!module) {
    ESP_LOGW(TAG, "Attempted to set NULL module");
    return;
  }

  if (current_draw_module && current_draw_module->teardown_func) current_draw_module->teardown_func();

  if (canvas) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (layer.draw_buf) {
      lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
      lv_canvas_finish_layer(canvas, &layer);
      lv_obj_invalidate(canvas);
    }
  }

  current_draw_module = module;
  ESP_LOGI(TAG, "Switched to module: %s", module->name);

  if (module->init_func) module->init_func();

  if (module->draw_func) module->draw_func();
}

ui_draw_module_t* ui_get_current_module(void) {
  return current_draw_module;
}

static void deferred_canvas_hide_cb(lv_timer_t *timer) {
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_timer_del(timer);
}

static void deferred_canvas_show_cb(lv_timer_t *timer) {
  if (canvas != NULL) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    // Only invalidate if not currently rendering to prevent feedback loops
    lv_display_t *disp = lv_obj_get_disp(canvas);
    if (disp && !disp->rendering_in_progress) {
      lv_obj_invalidate(canvas);
    }
  }
  lv_timer_del(timer);
}

void ui_init(void) {
  // Allocate display buffer
  size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
  size_t buf_size = 128 * 128 * bytes_per_pixel;
  display_buf = lv_malloc(buf_size);
  if (!display_buf) {
    ESP_LOGE(TAG, "Failed to allocate display buffer!");
    return;
  }
  
  // Standard canvas - using native color format
  canvas = lv_canvas_create(lv_scr_act());
  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);
  
  memset(display_buf, 0, buf_size);
  lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_NATIVE);
  ESP_LOGI(TAG, "Canvas created: %d bytes", (int)buf_size);

  g_ui_refresh_timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps

  g_app_mode = APP_MODE_PERFORMANCE;
  g_at_programming_top_level_menu = false;
  
  ui_event_handler_init();
}

app_mode_t ui_get_app_mode(void) {
  return g_app_mode;
}

void ui_set_app_mode(app_mode_t mode) {
  app_mode_t previous_mode = g_app_mode;
  g_app_mode = mode;
  
  const char* mode_names[] = {"Performance", "Programming", "Screensaver"};
  const char* prev_name = (previous_mode < 3) ? mode_names[previous_mode] : "Unknown";
  const char* new_name = (mode < 3) ? mode_names[mode] : "Unknown";
  
  ESP_LOGI(TAG, "App mode changed: %s -> %s", prev_name, new_name);
}

bool ui_is_programming_top_level(void) {
  return g_at_programming_top_level_menu;
}

void ui_set_programming_top_level(bool is_top_level) {
  g_at_programming_top_level_menu = is_top_level;
  ESP_LOGI(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
}

void ui_graphics_suspend(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);

  // Defer canvas hiding to avoid blocking in timer callback context
  lv_timer_t *hide_timer = lv_timer_create(deferred_canvas_hide_cb, 1, NULL);
  if (hide_timer != NULL) lv_timer_set_repeat_count(hide_timer, 1);
}

void ui_graphics_resume(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);

  // Defer canvas showing to avoid blocking in timer callback context
  lv_timer_t *show_timer = lv_timer_create(deferred_canvas_show_cb, 1, NULL);
  if (show_timer != NULL) lv_timer_set_repeat_count(show_timer, 1);
}

// Deferred callback to tear down current module and free canvas buffer
static void deferred_module_teardown_cb(lv_timer_t *timer) {
  // Guard against multiple teardowns
  if (g_teardown_in_progress) {
    ESP_LOGW(TAG, "Teardown already in progress, skipping");
    lv_timer_del(timer);
    return;
  }
  
  g_teardown_in_progress = true;
  
  // First, hide the canvas to stop any further rendering
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  
  if (current_draw_module && current_draw_module->teardown_func) {
    // Switch back to the default screen before tearing down
    // This prevents "active screen deleted" warnings and crashes
    lv_obj_t *default_screen = lv_obj_get_parent(canvas);
    if (default_screen && lv_obj_is_valid(default_screen)) lv_scr_load(default_screen);
    
    current_draw_module->teardown_func();
    ESP_LOGD(TAG, "Tore down module '%s' to reduce fragmentation", current_draw_module->name);
  }
  
  // Now free the canvas buffer after widgets are safely torn down
  if (display_buf) {
    lv_mem_monitor_t mon_before;
    lv_mem_monitor(&mon_before);
    ESP_LOGI(TAG, "LVGL memory before UI free - used: %d, free: %d, frag: %d%%", 
      mon_before.total_size - mon_before.free_size, mon_before.free_size, mon_before.frag_pct);
    
    lv_free(display_buf);
    display_buf = NULL;
    
    lv_mem_monitor_t mon_after;
    lv_mem_monitor(&mon_after);
    ESP_LOGI(TAG, "UI canvas buffer freed (32KB)");
    ESP_LOGI(TAG, "LVGL memory after UI free - used: %d, free: %d, frag: %d%%", 
      mon_after.total_size - mon_after.free_size, mon_after.free_size, mon_after.frag_pct);
  }
  
  lv_timer_del(timer);
}

void ui_release_canvas_buffer(void) {
  ESP_LOGD(TAG, "Releasing UI canvas buffer (32KB)...");
  
  // Guard against multiple releases
  if (g_teardown_in_progress) {
    ESP_LOGW(TAG, "Teardown already in progress, ignoring release request");
    return;
  }
  
  // Check current memory state
  lv_mem_monitor_t mon_initial;
  lv_mem_monitor(&mon_initial);
  ESP_LOGI(TAG, "LVGL memory at start of release - used: %d, free: %d, frag: %d%%", 
    mon_initial.total_size - mon_initial.free_size, mon_initial.free_size, mon_initial.frag_pct);
  
  // Pause the refresh timer to prevent new render cycles
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);
  
  // Defer ALL UI modifications to LVGL context
  // This ensures any in-flight render completes before we make changes
  // The 10ms delay allows the current render cycle to finish
  lv_timer_t *teardown_timer = lv_timer_create(deferred_module_teardown_cb, 10, NULL);
  if (teardown_timer) lv_timer_set_repeat_count(teardown_timer, 1);
  
  // Note: Canvas is hidden in the deferred callback, not here
  // Note: Buffer is freed in the deferred callback, not here
}

// Deferred callback to recreate module widgets
static void deferred_module_recreate_cb(lv_timer_t *timer) {
  if (current_draw_module && current_draw_module->draw_func) {
    current_draw_module->draw_func();
    ESP_LOGD(TAG, "Recreated module '%s' widgets", current_draw_module->name);
  }
  lv_timer_del(timer);
}

void ui_reclaim_canvas_buffer(void) {
  ESP_LOGD(TAG, "Reclaiming UI canvas buffer...");
  
  // Reallocate the buffer if needed
  if (!display_buf) {
    size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    size_t buf_size = 128 * 128 * bytes_per_pixel;
    display_buf = lv_malloc(buf_size);
    if (!display_buf) {
      ESP_LOGE(TAG, "Failed to reallocate display buffer!");
      g_teardown_in_progress = false;  // Clear flag on error
      return;
    }
    memset(display_buf, 0, buf_size);
  }
  
  // Restore the canvas buffer
  if (canvas != NULL && display_buf != NULL) {
    lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_NATIVE);
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    // Don't invalidate here - let the draw function handle it
  }
  
  // Resume the refresh timer
  if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);
  
  // Clear teardown flag before recreating widgets
  g_teardown_in_progress = false;
  
  // Defer widget recreation to LVGL context with a delay to allow memory to settle
  lv_timer_t *recreate_timer = lv_timer_create(deferred_module_recreate_cb, 50, NULL);
  if (recreate_timer) lv_timer_set_repeat_count(recreate_timer, 1);
  
  ESP_LOGD(TAG, "UI canvas buffer reclaimed");
}