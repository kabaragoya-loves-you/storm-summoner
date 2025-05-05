#include "lvgl.h"
#include "ui.h"

static lv_obj_t *canvas = NULL;

// LVGL canvas buffer (16-bit color)
static lv_color_t display_buf[128 * 128];

void boundary_circle(void) {
    lv_obj_t *circle = lv_obj_create(lv_scr_act());
    lv_obj_set_size(circle, 128, 128);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_white(), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
}

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  boundary_circle();
  lv_obj_invalidate(canvas);
}

void ui_init(void) {
  canvas = lv_canvas_create(lv_scr_act());

  lv_canvas_set_buffer(
    canvas,
    display_buf,
    128,
    128,
    LV_COLOR_FORMAT_NATIVE
  );

  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);
  lv_timer_create(lvgl_timer_cb, 16, NULL);
}
