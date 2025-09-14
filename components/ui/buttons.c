#include "lvgl.h"
#include "ui.h"
#include "ui_compositor.h"
#include "ui_layers.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"

#define BUTTONS_CENTER_X 64
#define BUTTONS_CENTER_Y 64
#define BUTTONS_GLOBE_RADIUS 25.0f
#define BUTTONS_GLOBE_SCALE 0.8f
#define BUTTONS_ANIMATION_FPS 18
#define BUTTONS_TIMER_MS (1000 / BUTTONS_ANIMATION_FPS)
#define BUTTONS_ROTATION_SPEED_X 0.020f
#define BUTTONS_ROTATION_SPEED_Y 0.010f
#define BUTTONS_ROTATION_SPEED_Z 0.0f
#define TAG "BUTTONS"

extern lv_obj_t *canvas;
extern const lv_image_dsc_t earth;

static float rotation_x = 0.0f;
static float rotation_y = 0.0f;
static float rotation_z = 0.0f;
static lv_timer_t *rotation_timer = NULL;

// Structure to hold globe exclusion data
typedef struct {
  float center_x;
  float center_y;
  float radius_squared;
} globe_exclusion_data_t;


// Exclusion check function for globe
static bool globe_exclusion_check(float x, float y, void* user_data) {
  globe_exclusion_data_t* data = (globe_exclusion_data_t*)user_data;
  if (!data) return false;
  
  float dx = x - data->center_x;
  float dy = y - data->center_y;
  float dist2 = dx * dx + dy * dy;
  return dist2 < data->radius_squared;
}

// State provider for slices based on touch input
static bool touch_slice_state_provider(uint8_t slice_index, void* user_data) {
  return ui_touch_is_button_pressed(slice_index);
}

// Combined exclusion check for both globe and slices
static bool buttons_combined_exclusion_check(float x, float y, void* user_data) {
  // Check globe exclusion
  if (globe_exclusion_check(x, y, user_data)) return true;
  
  // Check slices exclusion (uses internally stored context)
  if (slices_exclusion_check(x, y, NULL)) return true;
  
  return false;
}

static void buttons_rotation_cb(lv_timer_t *timer) {
  rotation_x += BUTTONS_ROTATION_SPEED_X;
  rotation_y += BUTTONS_ROTATION_SPEED_Y;
  rotation_z += BUTTONS_ROTATION_SPEED_Z;
  if (rotation_x > 2.0f * M_PI) rotation_x -= 2.0f * M_PI;
  if (rotation_y > 2.0f * M_PI) rotation_y -= 2.0f * M_PI;
  if (rotation_z > 2.0f * M_PI) rotation_z -= 2.0f * M_PI;
  
  // Update animations
  starfield_update();
  
  if (!canvas) return;
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) return;
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  // Draw globe
  globe_draw(canvas, BUTTONS_CENTER_X, BUTTONS_CENTER_Y, BUTTONS_GLOBE_RADIUS, 
    rotation_x, rotation_y, rotation_z, BUTTONS_GLOBE_SCALE);
  
  // Draw slices
  slices_draw(canvas, &layer, touch_slice_state_provider, NULL);
  
  // Set up exclusion zones for starfield
  globe_exclusion_data_t globe_data = {
    .center_x = BUTTONS_CENTER_X,
    .center_y = BUTTONS_CENTER_Y,
    .radius_squared = BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_RADIUS * BUTTONS_GLOBE_SCALE * BUTTONS_GLOBE_SCALE
  };
  
  // Configure slices exclusion context (it stores its own state internally)
  slices_set_exclusion_context(touch_slice_state_provider, NULL);
  
  // Create exclusion check wrapper that handles both globe and slices
  starfield_exclusion_check_fn exclusion_check = buttons_combined_exclusion_check;
  
  // Draw starfield with combined exclusion check
  starfield_draw(canvas, &exclusion_check, 1, &globe_data);
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}

static void buttons_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }
  if (!rotation_timer) {
    rotation_timer = lv_timer_create(buttons_rotation_cb, BUTTONS_TIMER_MS, NULL);
    lv_timer_set_repeat_count(rotation_timer, -1);
  }
  // Draw the first frame immediately
  buttons_rotation_cb(NULL);
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(buttons, buttons_draw_deferred_cb)

static void buttons_teardown(void) {
  if (rotation_timer) {
    lv_timer_del(rotation_timer);
    rotation_timer = NULL;
  }
  starfield_deinit();
  slices_deinit();
}

static void buttons_init(void) {
  globe_init();
  starfield_init();
  slices_init();
  rotation_x = rotation_y = rotation_z = 0.0f;
}

ui_draw_module_t buttons_module = {
  .draw_func = buttons_draw,
  .teardown_func = buttons_teardown,
  .init_func = buttons_init,
  .name = "buttons"
}; 