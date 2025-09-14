#include "lvgl.h"
#include "ui.h"
#include "lv_sphere2.h"
#include "esp_log.h"

#define TAG "SPHERE2"

extern lv_obj_t *canvas;

// Widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_sphere2 = NULL;

// Public API functions that delegate to widget
void sphere2_set_rotation(float speed_x, float speed_y, float speed_z, 
                         float dir_x, float dir_y, float dir_z) {
  if (g_sphere2) {
    lv_sphere2_set_rotation_speed(g_sphere2, speed_x, speed_y, speed_z);
    lv_sphere2_set_rotation_direction(g_sphere2, dir_x, dir_y, dir_z);
    ESP_LOGI(TAG, "Rotation set: speeds(%.3f, %.3f, %.3f) directions(%.3f, %.3f, %.3f)", 
      speed_x, speed_y, speed_z, dir_x, dir_y, dir_z);
  }
}

void sphere2_set_scale(float scale) {
  if (g_sphere2) {
    lv_sphere2_set_scale(g_sphere2, scale);
    ESP_LOGI(TAG, "Sphere2 scale set to %.3f", scale);
  }
}

static void sphere2_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  
  lv_display_t *disp = lv_obj_get_display(canvas);
  if (!disp) {
    ESP_LOGE(TAG, "Failed to get display from canvas");
    lv_timer_del(timer);
    return;
  }
  
  // Create screen
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, 128, 128);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Create sphere2 widget
  g_sphere2 = lv_sphere2_create(g_screen);
  lv_obj_set_size(g_sphere2, 128, 128);
  lv_obj_align(g_sphere2, LV_ALIGN_CENTER, 0, 0);
  
  // Configure sphere2
  lv_sphere2_set_radius(g_sphere2, 25);
  lv_sphere2_set_scale(g_sphere2, 0.8f);
  lv_sphere2_set_rotation_speed(g_sphere2, 0.020f, 0.010f, 0.0f);
  lv_sphere2_set_rotation_direction(g_sphere2, 1.0f, 1.0f, 1.0f);
  
  // Configure lighting
  lv_sphere2_set_light_direction(g_sphere2, 0.577f, 0.577f, 0.577f);
  lv_sphere2_set_light_intensity(g_sphere2, 0.4f, 0.6f);
  
  // Configure halo
  lv_sphere2_set_halo(g_sphere2, true, 1, 1);
  
  // Configure starfield
  lv_sphere2_set_starfield(g_sphere2, 40, 4);
  
  // Set detail level
  lv_sphere2_set_detail(g_sphere2, 16, 10);
  
  // Start animation
  lv_sphere2_set_animated(g_sphere2, true);
  
  // Load screen
  lv_screen_load(g_screen);
  
  ESP_LOGI(TAG, "Sphere2 screen created with textured planet and starfield");
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(sphere2, sphere2_draw_deferred_cb)

static void sphere2_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_sphere2 = NULL;
  }
  ESP_LOGD(TAG, "Sphere2 module teardown complete");
}

static void sphere2_init(void) {
  ESP_LOGI(TAG, "Sphere2 module initialized");
}

ui_draw_module_t sphere2_module = {
  .draw_func = sphere2_draw,
  .teardown_func = sphere2_teardown,
  .init_func = sphere2_init,
  .name = "sphere2"
};
