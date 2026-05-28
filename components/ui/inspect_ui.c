#include "inspect_ui.h"
#include "display_driver.h"
#include <stdio.h>

void inspect_ui_cache_scroll_metrics(lv_obj_t *label, lv_obj_t *scroll_cont,
    int32_t *out_line_step_px, int32_t *out_pad_top_px) {
  const lv_font_t *font = &lv_font_montserrat_14;
  int32_t line_space = 0;

  if (label && lv_obj_is_valid(label)) {
    const lv_font_t *label_font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    if (label_font) font = label_font;
    line_space = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
  }

  int32_t line_step = lv_font_get_line_height(font) + line_space;
  if (line_step <= 0) line_step = lv_font_get_line_height(font);

  int32_t pad_top = 4;
  if (scroll_cont && lv_obj_is_valid(scroll_cont)) {
    pad_top = lv_obj_get_style_pad_top(scroll_cont, LV_PART_MAIN);
  }

  if (out_line_step_px) *out_line_step_px = line_step;
  if (out_pad_top_px) *out_pad_top_px = pad_top;
}

lv_obj_t *inspect_ui_create(lv_obj_t *parent, const char *text, uint8_t scene_index,
    lv_obj_t **out_scroll_cont) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  const int title_bar_h = 32;
  const int left_margin = 12;

  char title[24];
  snprintf(title, sizeof(title), "Scene %u", (unsigned)(scene_index + 1));

  lv_obj_set_size(parent, disp_w, disp_h);
  lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(parent, 0, 0);
  lv_obj_set_style_pad_all(parent, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(parent);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);
  lv_obj_remove_flag(title_label, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *scroll_cont = lv_obj_create(parent);
  lv_obj_set_size(scroll_cont, disp_w - 4, disp_h - title_bar_h - 4);
  lv_obj_align(scroll_cont, LV_ALIGN_TOP_LEFT, 2, title_bar_h + 2);
  lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scroll_cont, 0, 0);
  lv_obj_set_style_pad_left(scroll_cont, left_margin, 0);
  lv_obj_set_style_pad_top(scroll_cont, 4, 0);
  lv_obj_set_style_pad_right(scroll_cont, left_margin, 0);
  lv_obj_set_style_pad_bottom(scroll_cont, 8, 0);
  lv_obj_set_scroll_dir(scroll_cont, LV_DIR_VER);
  lv_obj_add_flag(scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(scroll_cont, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(scroll_cont, 0, LV_STATE_FOCUSED);

  lv_obj_t *label = lv_label_create(scroll_cont);
  lv_label_set_text(label, text ? text : "");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_width(label, disp_w - (left_margin * 2) - 8);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_update_layout(scroll_cont);
  lv_obj_scroll_to(scroll_cont, 0, 0, LV_ANIM_OFF);

  if (out_scroll_cont) *out_scroll_cont = scroll_cont;
  return parent;
}
