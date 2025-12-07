#include "lvgl.h"
#include "ui.h"
#include "lv_vector_art.h"
#include "esp_log.h"

#define TAG "TEMPO"

//=============================================================================
// TEMPO MODULE CONFIGURATION
//=============================================================================

// Sunburst gradient colors (gold center to darker gold edge)
#define TEMPO_CENTER_R  255
#define TEMPO_CENTER_G  200
#define TEMPO_CENTER_B  100

#define TEMPO_EDGE_R    180
#define TEMPO_EDGE_G    100
#define TEMPO_EDGE_B    40

// Animated vector art path
#define TEMPO_ANIM_PATH "/assets/images/kabaragoya_anim.bin.z"

//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_vector_art = NULL;
static lv_grad_dsc_t g_grad;

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

static void tempo_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }
  
  lv_display_t *disp = lv_display_get_default();
  g_disp_width = lv_display_get_horizontal_resolution(disp);
  g_disp_height = lv_display_get_vertical_resolution(disp);
  
  // Create screen with radial gradient background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  
  // Layer 1: Radial gradient background (sunburst)
  lv_grad_radial_init(&g_grad, 
    LV_PCT(50), LV_PCT(50),
    LV_PCT(100), LV_PCT(50),
    LV_GRAD_EXTEND_PAD);
  
  lv_color_t colors[2] = {
    lv_color_make(TEMPO_CENTER_R, TEMPO_CENTER_G, TEMPO_CENTER_B),
    lv_color_make(TEMPO_EDGE_R, TEMPO_EDGE_G, TEMPO_EDGE_B)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_screen, &g_grad, 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Layer 2: Animated vector art
  g_vector_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_vector_art, g_disp_width, g_disp_height);
  lv_obj_center(g_vector_art);
  
  // Scale to fit display (source is 198x198)
  float scale = (float)g_disp_width / 198.0f;
  lv_vector_art_set_scale(g_vector_art, scale);
  
  // Load the animated vector art
  if (lv_vector_art_set_src(g_vector_art, TEMPO_ANIM_PATH)) {
    if (lv_vector_art_is_animated(g_vector_art)) {
      ESP_LOGI(TAG, "Loaded animated vector art: %d frames @ %d fps",
               lv_vector_art_get_frame_count(g_vector_art),
               lv_vector_art_get_fps(g_vector_art));
      
      // Auto-start looping animation
      lv_vector_art_play(g_vector_art);
    } else {
      ESP_LOGW(TAG, "Loaded static vector art (expected animated)");
    }
  } else {
    ESP_LOGE(TAG, "Failed to load animated vector art from %s", TEMPO_ANIM_PATH);
  }
  
  ESP_LOGI(TAG, "Tempo screen created with animated vector art");
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(tempo, tempo_draw_deferred_cb)

static void tempo_teardown(void) {
  if (g_screen) {
    // Animation timer is cleaned up by widget destructor
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_vector_art = NULL;
  }
  ESP_LOGD(TAG, "Tempo teardown complete");
}

static void tempo_init(void) {
  ESP_LOGI(TAG, "Tempo module initialized");
}

ui_draw_module_t tempo_module = {
  .draw_func = tempo_draw,
  .teardown_func = tempo_teardown,
  .init_func = tempo_init,
  .name = "tempo"
};

