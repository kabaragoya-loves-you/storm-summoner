#include "lvgl.h"
#include "ui.h"
#include "ui_module_settings.h"
#include "lv_boundary_circle.h"
#include "shared_canvas_buffer.h"
#include "esp_log.h"

#define TAG "BOUNDARY_CIRCLE"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_boundary = NULL;

// Calibration values - 0 means "use display dimensions"
static int32_t g_circle_size = 0;     // 0 = use viewport size
static int32_t g_circle_center_x = 0; // 0 = center of viewport
static int32_t g_circle_center_y = 0; // 0 = center of viewport

// Forward declaration for callback
static void boundary_circle_update_position(void);

// Module settings
static ui_module_setting_t boundary_circle_settings[] = {
  { "size", UI_SETTING_I32, &g_circle_size, "Circle diameter (0=auto)", boundary_circle_update_position, NULL },
  { "left", UI_SETTING_I32, &g_circle_center_x, "Center X position (0=center)", boundary_circle_update_position, NULL },
  { "top", UI_SETTING_I32, &g_circle_center_y, "Center Y position (0=center)", boundary_circle_update_position, NULL },
};
static const size_t boundary_circle_settings_count = 
  sizeof(boundary_circle_settings) / sizeof(boundary_circle_settings[0]);

static void boundary_circle_update_position(void) {
  if (!g_boundary || !g_screen) return;
  
  int disp_width = shared_canvas_buffer_get_width();
  int disp_height = shared_canvas_buffer_get_height();
  
  // Use viewport dimensions if not explicitly set
  int32_t size = g_circle_size > 0 ? g_circle_size : disp_width;
  int32_t center_x = g_circle_center_x > 0 ? g_circle_center_x : (disp_width / 2);
  int32_t center_y = g_circle_center_y > 0 ? g_circle_center_y : (disp_height / 2);
  
  // Calculate offset from display center
  int32_t offset_x = center_x - (disp_width / 2);
  int32_t offset_y = center_y - (disp_height / 2);
  
  lv_obj_set_size(g_boundary, size, size);
  lv_obj_align(g_boundary, LV_ALIGN_CENTER, offset_x, offset_y);
  lv_obj_invalidate(g_boundary);
  
  ESP_LOGI(TAG, "Circle: size=%ld, center=(%ld,%ld) offset=(%ld,%ld)", 
    size, center_x, center_y, offset_x, offset_y);
}

static void boundary_circle_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  int disp_width = shared_canvas_buffer_get_width();
  int disp_height = shared_canvas_buffer_get_height();
  
  // Only create widgets if they don't exist
  if (g_screen == NULL) {
    // Get the display from canvas
    lv_display_t *disp = lv_obj_get_display(canvas);
    if (!disp) {
      ESP_LOGE(TAG, "Failed to get display from canvas");
      lv_timer_delete(timer);
      return;
    }
    
    // Create a screen sized to the display
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_width, disp_height);
    
    // Set black background
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    
    // Create boundary circle widget
    g_boundary = lv_boundary_circle_create(g_screen);
    
    // Configure to match original (white, 1px width, margin of 1)
    lv_boundary_circle_set_color(g_boundary, lv_color_white());
    lv_boundary_circle_set_width(g_boundary, 1);
    lv_boundary_circle_set_margin(g_boundary, 1);
    
    // Apply calibration position
    boundary_circle_update_position();
    
    ESP_LOGI(TAG, "Boundary circle screen created (%dx%d)", disp_width, disp_height);
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
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

static void boundary_circle_init(void) {
  ui_module_register_settings("boundary_circle", boundary_circle_settings, 
                               boundary_circle_settings_count);
}

ui_draw_module_t boundary_circle_module = {
  .draw_func = boundary_circle_draw,
  .teardown_func = boundary_circle_teardown,
  .init_func = boundary_circle_init,
  .name = "boundary_circle",
  .title = "Boundary Circle"
};

// Calibration API
void boundary_circle_set_size(int32_t size) {
  g_circle_size = size;
  boundary_circle_update_position();
}

void boundary_circle_set_left(int32_t x) {
  g_circle_center_x = x;
  boundary_circle_update_position();
}

void boundary_circle_set_top(int32_t y) {
  g_circle_center_y = y;
  boundary_circle_update_position();
}

int32_t boundary_circle_get_size(void) { return g_circle_size; }
int32_t boundary_circle_get_left(void) { return g_circle_center_x; }
int32_t boundary_circle_get_top(void) { return g_circle_center_y; }
