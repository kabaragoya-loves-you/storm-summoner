#include "lvgl.h"
#include "ui.h"
#include "lv_plasma.h"
#include "shared_canvas_buffer.h"
#include "touch.h"
#include "esp_log.h"
#include <math.h>

#define TAG "PLASMA"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

//=============================================================================
// PLASMA MODULE CONFIGURATION
//=============================================================================

// Tendril appearance
#define PLASMA_SEGMENTS       10       // Segments per tendril (fewer = faster)
#define PLASMA_DISPLACEMENT   20.0f   // Jaggedness (pixels)
#define PLASMA_ANIM_SPEED     4.0f    // Animation speed multiplier

// Pad positions
#define PLASMA_PAD_RADIUS     0.45f   // Pad distance from center (fraction of display)

// Center point of plasma globe (where tendrils originate)
// Set to -1 to use display center, or specify exact pixel coordinates
#define PLASMA_CENTER_X       -1      // X coordinate (-1 = display center)
#define PLASMA_CENTER_Y       -1      // Y coordinate (-1 = display center)

// Background image
#define PLASMA_ENABLE_BACKGROUND  0   // 1 = show kabaragoya image, 0 = black background

//=============================================================================

extern lv_obj_t *canvas;

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_plasma = NULL;

// Keypad positions (8 pads arranged in a circle)
static int32_t pad_positions[8][2];
static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

static void calculate_pad_positions(uint16_t width, uint16_t height, int32_t center_x, int32_t center_y) {
  // Pads are arranged in a circle around the plasma center
  // Starting offset by 22.5° from top, going clockwise
  float radius = (width < height ? width : height) * PLASMA_PAD_RADIUS;
  
  for (int i = 0; i < 8; i++) {
    // Angle: start at -67.5° (22.5° clockwise from 12 o'clock) and go clockwise
    float angle = (-67.5f + i * 45.0f) * M_PI / 180.0f;
    pad_positions[i][0] = center_x + (int32_t)(cosf(angle) * radius);
    pad_positions[i][1] = center_y + (int32_t)(sinf(angle) * radius);
  }
}

// State callback - returns true if tendril should be active
static bool plasma_state_cb(uint8_t tendril_index, void* user_data) {
  (void)user_data;
  return touch_is_pad_pressed(tendril_index);
}

// Target callback - returns x,y position for tendril endpoint
static void plasma_target_cb(uint8_t tendril_index, int32_t* x, int32_t* y, void* user_data) {
  (void)user_data;
  if (tendril_index < 8) {
    *x = pad_positions[tendril_index][0];
    *y = pad_positions[tendril_index][1];
  }
}

static void plasma_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    lv_display_t *disp = lv_obj_get_display(canvas);
    if (!disp) {
      ESP_LOGE(TAG, "Failed to get display from canvas");
      lv_timer_delete(timer);
      return;
    }
    
    g_disp_width = shared_canvas_buffer_get_width();
    g_disp_height = shared_canvas_buffer_get_height();
    
    // Calculate center point (where tendrils originate)
    int32_t center_x = (PLASMA_CENTER_X < 0) ? g_disp_width / 2 : PLASMA_CENTER_X;
    int32_t center_y = (PLASMA_CENTER_Y < 0) ? g_disp_height / 2 : PLASMA_CENTER_Y;
    
    // Calculate pad positions around the plasma center
    calculate_pad_positions(g_disp_width, g_disp_height, center_x, center_y);
    
    // Create screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
    lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
    
#if PLASMA_ENABLE_BACKGROUND
    // Use image as screen background (more efficient than image widget)
    lv_obj_set_style_bg_image_src(g_screen, "A:images/kabaragoya.bin", 0);
    lv_obj_set_style_bg_image_opa(g_screen, LV_OPA_COVER, 0);
#endif
    
    // Create plasma widget
    g_plasma = lv_plasma_create(g_screen);
    lv_obj_set_size(g_plasma, g_disp_width, g_disp_height);
    lv_obj_align(g_plasma, LV_ALIGN_CENTER, 0, 0);
    
    // Set center point (same as used for pad positions)
    lv_plasma_set_center(g_plasma, center_x, center_y);
    
    // Configure appearance
    lv_plasma_set_style(g_plasma, PLASMA_SEGMENTS, PLASMA_DISPLACEMENT);
    lv_plasma_set_colors(g_plasma, 
                         lv_color_make(220, 180, 255),  // Light purple/pink core
                         lv_color_make(120, 60, 200));  // Deep purple glow
    lv_plasma_set_animation_speed(g_plasma, PLASMA_ANIM_SPEED);
    
    // Set callbacks to poll touch state (like lv_slices does)
    lv_plasma_set_state_cb(g_plasma, plasma_state_cb, plasma_target_cb, NULL);
    
    ESP_LOGI(TAG, "Plasma screen created at %dx%d", g_disp_width, g_disp_height);
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(plasma, plasma_draw_deferred_cb)

static void plasma_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_plasma = NULL;
  }
  ESP_LOGD(TAG, "Plasma module teardown complete");
}

static void plasma_init(void) {
  ESP_LOGI(TAG, "Plasma module initialized");
}

ui_draw_module_t plasma_module = {
  .draw_func = plasma_draw,
  .teardown_func = plasma_teardown,
  .init_func = plasma_init,
  .name = "plasma"
};

