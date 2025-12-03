#include "lvgl.h"
#include "ui.h"
#include "lv_vector_art.h"
#include "esp_log.h"

#define TAG "SPLASH"

//=============================================================================
// SPLASH MODULE CONFIGURATION
//=============================================================================

// Sunburst gradient colors (gold center to darker gold edge)
#define SPLASH_CENTER_R  255
#define SPLASH_CENTER_G  200
#define SPLASH_CENTER_B  100

#define SPLASH_EDGE_R    180
#define SPLASH_EDGE_G    100
#define SPLASH_EDGE_B    40

// Vector art overlay
#define SPLASH_VECTOR_PATH "/assets/images/kabaragoya_vec.bin.z"

//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_vector_art = NULL;
static lv_grad_dsc_t g_grad;

static void splash_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }
  
  lv_display_t *disp = lv_display_get_default();
  uint16_t disp_width = lv_display_get_horizontal_resolution(disp);
  uint16_t disp_height = lv_display_get_vertical_resolution(disp);
  
  // Create screen with radial gradient background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, disp_width, disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  
  // Set up radial gradient (sunburst effect)
  lv_grad_radial_init(&g_grad, 
    LV_PCT(50), LV_PCT(50),
    LV_PCT(100), LV_PCT(50),
    LV_GRAD_EXTEND_PAD);
  
  lv_color_t colors[2] = {
    lv_color_make(SPLASH_CENTER_R, SPLASH_CENTER_G, SPLASH_CENTER_B),
    lv_color_make(SPLASH_EDGE_R, SPLASH_EDGE_G, SPLASH_EDGE_B)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_screen, &g_grad, 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Create vector art widget
  g_vector_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_vector_art, disp_width, disp_height);
  lv_obj_center(g_vector_art);
  
  float scale = (float)disp_width / 198.0f;
  lv_vector_art_set_scale(g_vector_art, scale);
  
  if (lv_vector_art_set_src(g_vector_art, SPLASH_VECTOR_PATH)) {
    ESP_LOGI(TAG, "Splash screen created with vector art");
  } else {
    ESP_LOGE(TAG, "Failed to load vector art");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(splash, splash_draw_deferred_cb)

static void splash_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_vector_art = NULL;
  }
  ESP_LOGD(TAG, "Splash module teardown complete");
}

static void splash_init(void) {
  ESP_LOGI(TAG, "Splash module initialized");
}

ui_draw_module_t splash_module = {
  .draw_func = splash_draw,
  .teardown_func = splash_teardown,
  .init_func = splash_init,
  .name = "splash"
};

