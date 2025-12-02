#include "lvgl.h"
#include "ui.h"
#include "lv_radar.h"
#include "lv_slices.h"
#include "lv_globe.h"
#include "lv_starfield.h"
#include "shared_canvas_buffer.h"
#include "esp_log.h"

#define TAG "BUTTONS"

// Base dimensions for scaling (original 128x128 OLED)
#define BASE_SIZE 128

// Globe rotation speeds (radians per frame)
#define GLOBE_ROTATION_X  0.005f   // Vertical tilt speed
#define GLOBE_ROTATION_Y  0.02f    // Horizontal spin speed
#define GLOBE_ROTATION_Z  0.0f     // Roll speed

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_starfield = NULL;
static lv_obj_t *g_radar = NULL;
static lv_obj_t *g_slices = NULL;
static lv_obj_t *g_globe = NULL;

static void buttons_draw_deferred_cb(lv_timer_t *timer) {
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
    
    // Get dynamic display dimensions and calculate scale factor
    uint16_t disp_width = shared_canvas_buffer_get_width();
    uint16_t disp_height = shared_canvas_buffer_get_height();
    float scale = (float)disp_width / BASE_SIZE;
    
    // Scale radii and sizes proportionally
    int32_t inner_radius = (int32_t)(25 * scale);
    int32_t outer_radius = disp_width;  // Well past display edge to ensure full coverage
    int32_t radar_start = (int32_t)(30 * scale);
    int32_t radar_end = outer_radius;  // Match slices
    float globe_radius = 20.0f;  // Fixed size, not scaled (good for detail)
    
    // Create a screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_width, disp_height);
    
    // Set black background
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    
    // Create widgets in order (bottom to top)
    
    // Create starfield widget (bottom-most layer)
    g_starfield = lv_starfield_create(g_screen);
    lv_obj_set_size(g_starfield, disp_width, disp_height);
    lv_obj_align(g_starfield, LV_ALIGN_CENTER, 0, 0);
    
    // Configure starfield (scale count slightly with display size)
    int star_count = (int)(24 * scale);
    lv_starfield_set_count(g_starfield, star_count);
    lv_starfield_set_twinkle_variance(g_starfield, 4);
    lv_starfield_set_movement(g_starfield, 50, 300);
    lv_starfield_set_exclude_siblings(g_starfield, true);  // Auto-exclude other widgets
    
    // Create radar widget
    g_radar = lv_radar_create(g_screen);
    lv_obj_set_size(g_radar, disp_width, disp_height);
    lv_obj_align(g_radar, LV_ALIGN_CENTER, 0, 0);
    
    // Configure radar
    lv_radar_set_line_count(g_radar, 8);
    lv_radar_set_radius_range(g_radar, radar_start, radar_end);
    lv_radar_set_dot_pattern(g_radar, 8, 1);  // 8 pixel gaps, 1 pixel dots
    lv_radar_set_line_style(g_radar, lv_color_make(17, 17, 17), LV_OPA_COVER);
    
    // Create slices widget
    g_slices = lv_slices_create(g_screen);
    lv_obj_set_size(g_slices, disp_width, disp_height);
    lv_obj_align(g_slices, LV_ALIGN_CENTER, 0, 0);
    
    // Configure slices
    lv_slices_set_count(g_slices, 8);
    lv_slices_set_radius(g_slices, inner_radius, outer_radius);
    lv_slices_set_colors(g_slices, lv_color_make(102, 102, 102), lv_color_black());
    lv_slices_set_opacity(g_slices, LV_OPA_COVER, LV_OPA_TRANSP);
    // Use default state callback which checks touch input
    
    // Create globe widget (center, on top)
    g_globe = lv_globe_create(g_screen);
    lv_obj_align(g_globe, LV_ALIGN_CENTER, 0, 0);
    
    // Configure globe
    lv_globe_set_radius(g_globe, globe_radius);
    lv_globe_set_scale(g_globe, 1.0f);  // Full size within widget
    lv_globe_set_rotation_speed(g_globe, GLOBE_ROTATION_X, GLOBE_ROTATION_Y, GLOBE_ROTATION_Z);
    lv_globe_set_auto_rotate(g_globe, true);
    
    ESP_LOGI(TAG, "Buttons screen: %dx%d, inner=%d, outer=%d, globe_r=%.1f", 
             disp_width, disp_height, inner_radius, outer_radius, globe_radius);
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons, buttons_draw_deferred_cb)

static void buttons_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_starfield = NULL;
    g_radar = NULL;
    g_slices = NULL;
    g_globe = NULL;
  }
  ESP_LOGD(TAG, "Buttons LVGL module teardown complete");
}

static void buttons_init(void) {
  ESP_LOGI(TAG, "Buttons LVGL module initialized");
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
};
