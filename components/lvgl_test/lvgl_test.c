#include "lvgl.h"

void lvgl_test(void) {
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

  // Create a 64x64 box in the middle of the screen
  lv_obj_t *box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(box, 64, 64);
  lv_obj_center(box);    // or lv_obj_align(box, LV_ALIGN_CENTER, 0, 0)

  // lv_obj_remove_style_all(box);

  static lv_style_t style;
  lv_style_init(&style);

  lv_grad_dsc_t grad;
  grad.dir = LV_GRAD_DIR_HOR; // horizontal gradient
  grad.stops_count = 16;
  grad.stops[0].color = lv_color_black();
  grad.stops[1].color = lv_color_white();

  lv_style_set_bg_opa(&style, LV_OPA_COVER);
  lv_style_set_bg_grad(&style, &grad);

  lv_obj_add_style(box, &style, 0);
}