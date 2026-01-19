#include "lvgl.h"
#include "ui.h"
#include "lv_plasma.h"
#include "lv_vector_art.h"
#include "touch.h"
#include "esp_log.h"
#include <math.h>

#define TAG "SUMMONER"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

//=============================================================================
// SUMMONER MODULE CONFIGURATION
//=============================================================================

// Sunburst gradient colors (gold center to darker gold edge)
#define SUMMONER_CENTER_R  255
#define SUMMONER_CENTER_G  200
#define SUMMONER_CENTER_B  100

#define SUMMONER_EDGE_R    180
#define SUMMONER_EDGE_G    100
#define SUMMONER_EDGE_B    40

// Vector art overlay
#define SUMMONER_VECTOR_PATH "/assets/images/kabaragoya_vec.bin.z"

// Plasma settings
#define PLASMA_SEGMENTS       10
#define PLASMA_DISPLACEMENT   20.0f
#define PLASMA_ANIM_SPEED     4.0f
#define PLASMA_PAD_RADIUS     0.49f
// Plasma origin point - set to lizard's hand position on 240x240 display
// Use -1 for display center, or explicit coordinates for off-center origin
// TODO: Calibrate to actual hand position on new 240x240 lizard image
#define PLASMA_CENTER_X       82
#define PLASMA_CENTER_Y       88

//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_vector_art = NULL;
static lv_obj_t *g_plasma = NULL;
static lv_grad_dsc_t g_grad;

// Keypad positions (8 pads arranged in a circle)
static int32_t pad_positions[8][2];
static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

static void calculate_pad_positions(uint16_t width, uint16_t height) {
  // Pads are at fixed positions on the display edge (physical touchwheel locations)
  // Always relative to display center, regardless of plasma center
  int32_t display_center_x = width / 2;
  int32_t display_center_y = height / 2;
  float radius = (width < height ? width : height) * PLASMA_PAD_RADIUS;
  
  for (int i = 0; i < 8; i++) {
    float angle = (-67.5f + i * 45.0f) * M_PI / 180.0f;
    pad_positions[i][0] = display_center_x + (int32_t)(cosf(angle) * radius);
    pad_positions[i][1] = display_center_y + (int32_t)(sinf(angle) * radius);
  }
}

static bool plasma_state_cb(uint8_t tendril_index, void* user_data) {
  (void)user_data;
  return touch_is_pad_pressed(tendril_index);
}

static void plasma_target_cb(uint8_t tendril_index, int32_t* x, int32_t* y, void* user_data) {
  (void)user_data;
  if (tendril_index < 8) {
    *x = pad_positions[tendril_index][0];
    *y = pad_positions[tendril_index][1];
  }
}

static void summoner_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }
  
  lv_display_t *disp = lv_display_get_default();
  g_disp_width = lv_display_get_horizontal_resolution(disp);
  g_disp_height = lv_display_get_vertical_resolution(disp);
  
  // Calculate pad positions at display edge (fixed locations)
  calculate_pad_positions(g_disp_width, g_disp_height);
  
  // Plasma center can be offset from display center
  int32_t center_x = (PLASMA_CENTER_X < 0) ? g_disp_width / 2 : PLASMA_CENTER_X;
  int32_t center_y = (PLASMA_CENTER_Y < 0) ? g_disp_height / 2 : PLASMA_CENTER_Y;
  
  // Create screen with radial gradient background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  
  // Layer 1: Radial gradient background
  lv_grad_radial_init(&g_grad, 
    LV_PCT(50), LV_PCT(50),
    LV_PCT(100), LV_PCT(50),
    LV_GRAD_EXTEND_PAD);
  
  lv_color_t colors[2] = {
    lv_color_make(SUMMONER_CENTER_R, SUMMONER_CENTER_G, SUMMONER_CENTER_B),
    lv_color_make(SUMMONER_EDGE_R, SUMMONER_EDGE_G, SUMMONER_EDGE_B)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_screen, &g_grad, 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Layer 2: Vector art
  g_vector_art = lv_vector_art_create(g_screen);
  lv_obj_set_size(g_vector_art, g_disp_width, g_disp_height);
  lv_obj_center(g_vector_art);
  
  float scale = (float)g_disp_width / 240.0f;
  lv_vector_art_set_scale(g_vector_art, scale);
  lv_vector_art_set_src(g_vector_art, SUMMONER_VECTOR_PATH);
  
  // Layer 3: Plasma widget (on top)
  g_plasma = lv_plasma_create(g_screen);
  lv_obj_set_size(g_plasma, g_disp_width, g_disp_height);
  lv_obj_align(g_plasma, LV_ALIGN_CENTER, 0, 0);
  
  lv_plasma_set_center(g_plasma, center_x, center_y);
  lv_plasma_set_style(g_plasma, PLASMA_SEGMENTS, PLASMA_DISPLACEMENT);
  lv_plasma_set_colors(g_plasma, 
                       lv_color_make(220, 180, 255),
                       lv_color_make(120, 60, 200));
  lv_plasma_set_animation_speed(g_plasma, PLASMA_ANIM_SPEED);
  lv_plasma_set_state_cb(g_plasma, plasma_state_cb, plasma_target_cb, NULL);
  
  ESP_LOGI(TAG, "Summoner screen created with vector art");
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(summoner, summoner_draw_deferred_cb)

static void summoner_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_vector_art = NULL;
    g_plasma = NULL;
  }
  ESP_LOGD(TAG, "Summoner teardown complete");
}

static void summoner_init(void) {
  ESP_LOGI(TAG, "Summoner module initialized");
}

ui_draw_module_t summoner_module = {
  .draw_func = summoner_draw,
  .teardown_func = summoner_teardown,
  .init_func = summoner_init,
  .name = "summoner"
};

