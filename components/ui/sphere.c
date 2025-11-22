#include "lvgl.h"
#include "ui.h"
#include "lv_sphere.h"
#include "esp_log.h"

#define TAG "SPHERE"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_sphere = NULL;

// Public API functions that delegate to widget
void sphere_set_rotation(float speed_x, float speed_y, float speed_z, 
                        float dir_x, float dir_y, float dir_z) {
  if (g_sphere) {
    lv_sphere_set_rotation_speed(g_sphere, speed_x, speed_y, speed_z);
    lv_sphere_set_rotation_direction(g_sphere, dir_x, dir_y, dir_z);
    ESP_LOGI(TAG, "Rotation set: speeds(%.3f, %.3f, %.3f) directions(%.3f, %.3f, %.3f)", 
      speed_x, speed_y, speed_z, dir_x, dir_y, dir_z);
  }
}

void sphere_set_scale(float scale) {
  if (g_sphere) {
    lv_sphere_set_scale(g_sphere, scale);
    ESP_LOGI(TAG, "Sphere scale set to %.3f", scale);
  }
}

float sphere_get_scale(void) {
  // Return default scale if widget not created yet
  return 0.8f;
}


static void sphere_draw_deferred_cb(lv_timer_t *timer) {
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
    
    // Create sphere widget
    g_sphere = lv_sphere_create(g_screen);
    lv_obj_set_size(g_sphere, 128, 128);
    lv_obj_align(g_sphere, LV_ALIGN_CENTER, 0, 0);
    
    // Configure to match original
    lv_sphere_set_radius(g_sphere, 25);
    lv_sphere_set_scale(g_sphere, 0.8f);
    lv_sphere_set_detail(g_sphere, 10, 8);  // SPHERE_DIVISIONS_U, SPHERE_DIVISIONS_V
    lv_sphere_set_rotation_speed(g_sphere, 0.025f, 0.040f, 0.020f);
    lv_sphere_set_rotation_direction(g_sphere, 1.0f, 1.0f, 1.0f);
    lv_sphere_set_line_color(g_sphere, lv_color_make(0, 255, 255));  // Cyan
    lv_sphere_set_line_width(g_sphere, 1);
    lv_sphere_set_animated(g_sphere, true);
    
    ESP_LOGI(TAG, "Sphere screen created");
  }
  
  // Load the screen (safe to call multiple times)
  lv_screen_load(g_screen);
  
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(sphere, sphere_draw_deferred_cb)

static void sphere_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_sphere = NULL;
  }
  ESP_LOGD(TAG, "Sphere module teardown complete");
}

static void sphere_init(void) {
  ESP_LOGI(TAG, "Sphere module initialized");
}

ui_draw_module_t sphere_module = {
  .draw_func = sphere_draw,
  .teardown_func = sphere_teardown,
  .init_func = sphere_init,
  .name = "sphere"
};
