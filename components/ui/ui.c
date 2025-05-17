#include "lvgl.h"
#include "ui.h"

static lv_obj_t *canvas = NULL;
static lv_obj_t *circle = NULL;

// LVGL canvas buffer (16-bit color) - ensure proper alignment
static lv_color_t display_buf[128 * 128] __attribute__((aligned(4)));

void boundary_circle(void) {
  // Create circle only if it doesn't exist
  if (circle == NULL) {
    circle = lv_obj_create(lv_scr_act());
    lv_obj_set_size(circle, 128, 128);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_white(), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
  }
}

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  
  // Only redraw if canvas exists
  if (canvas != NULL) {
    // Clear the canvas
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    // Invalidate the canvas to trigger a redraw
    // LVGL will automatically handle dirty region tracking
    lv_obj_invalidate(canvas);
  }
}

void ui_init(void) {
  // Create canvas with proper buffer
  canvas = lv_canvas_create(lv_scr_act());
  if (canvas == NULL) {
    return;
  }

  lv_canvas_set_buffer(
    canvas,
    display_buf,
    128,
    128,
    LV_COLOR_FORMAT_NATIVE
  );

  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);
  
  // Create initial circle
  boundary_circle();
  
  // Create timer with error checking
  // Using a longer refresh rate since we don't need 60fps for a static circle
  lv_timer_t *timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps
  if (timer == NULL) {
    // Handle timer creation failure
    lv_obj_del(canvas);
    canvas = NULL;
  }
}
