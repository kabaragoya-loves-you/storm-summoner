#include "menu.h"
#include "menu_pages.h"
#include "scene_inspect.h"
#include "scene.h"
#include "display_driver.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "INSPECT_SCENE"
#define INSPECT_TEXT_BUF_SIZE 2048

// Touch events use logical pad indices; on this PCB Alpha is logical 9, Gamma is 11.
#define INSPECT_PAD_ALPHA_LOGICAL 9
#define INSPECT_PAD_GAMMA_LOGICAL 11

static bool g_inspect_active = false;
static bool g_scene_subscribed = false;
static char s_inspect_text[INSPECT_TEXT_BUF_SIZE];
static lv_obj_t *s_scroll_cont = NULL;

static void scene_changed_refresh_handler(const event_t *event, void *context) {
  (void)event;
  (void)context;
  if (!g_inspect_active) return;
  menu_replace_current_deferred("Inspect Scene", menu_page_inspect_scene_create);
}

static void subscribe_scene_changed(void) {
  if (g_scene_subscribed) return;
  event_bus_subscribe(EVENT_SCENE_CHANGED, scene_changed_refresh_handler, NULL);
  g_scene_subscribed = true;
}

static void unsubscribe_scene_changed(void) {
  if (!g_scene_subscribed) return;
  event_bus_unsubscribe(EVENT_SCENE_CHANGED, scene_changed_refresh_handler);
  g_scene_subscribed = false;
}

bool inspect_scene_is_active(void) {
  return g_inspect_active;
}

void inspect_scene_rebind_input(void) {
  if (!s_scroll_cont) return;

  lv_group_t *group = menu_get_group();
  if (!group) return;

  lv_group_remove_all_objs(group);
  lv_group_add_obj(group, s_scroll_cont);
  lv_group_focus_obj(s_scroll_cont);
  lv_group_set_editing(group, true);
  ui_attach_encoder_to_menu();
}

static int32_t inspect_line_step_px(void) {
  if (!s_scroll_cont) return 0;

  lv_obj_t *label = lv_obj_get_child(s_scroll_cont, 0);
  if (!label) return 0;

  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (!font) font = &lv_font_montserrat_14;

  int32_t step = lv_font_get_line_height(font) +
    lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
  return step > 0 ? step : lv_font_get_line_height(font);
}

bool inspect_scene_jog_scroll(uint8_t pad_id) {
  if (!g_inspect_active || !s_scroll_cont) return false;
  if (pad_id != INSPECT_PAD_GAMMA_LOGICAL && pad_id != INSPECT_PAD_ALPHA_LOGICAL) return false;

  int32_t step = inspect_line_step_px();
  if (step <= 0) return false;

  lv_obj_update_layout(s_scroll_cont);
  int32_t y = lv_obj_get_scroll_y(s_scroll_cont);

  // Alpha (logical 9): scroll up in document — text moves down on screen. No-op at initial position.
  if (pad_id == INSPECT_PAD_ALPHA_LOGICAL) {
    if (y == 0) return false;

    int32_t before = y;
    lv_obj_scroll_to_y(s_scroll_cont, y - step, LV_ANIM_OFF);
    return lv_obj_get_scroll_y(s_scroll_cont) != before;
  }

  // Gamma (logical 11): scroll down in document — text moves up on screen (extra nudge at top).
  if (lv_obj_get_scroll_bottom(s_scroll_cont) <= 0 && y != 0) return false;

  int32_t target = y + step;
  if (y == 0) {
    target += lv_obj_get_style_pad_top(s_scroll_cont, LV_PART_MAIN);
  }

  int32_t before = y;
  lv_obj_scroll_to_y(s_scroll_cont, target, LV_ANIM_OFF);
  return lv_obj_get_scroll_y(s_scroll_cont) != before;
}

static lv_obj_t *create_inspect_screen(const char *text, uint8_t scene_index) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  const int title_bar_h = 32;
  const int left_margin = 12;

  char title[24];
  snprintf(title, sizeof(title), "Scene %u", (unsigned)(scene_index + 1));

  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  lv_obj_t *title_bar = lv_obj_create(screen);
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

  s_scroll_cont = lv_obj_create(screen);
  lv_obj_set_size(s_scroll_cont, disp_w - 4, disp_h - title_bar_h - 4);
  lv_obj_align(s_scroll_cont, LV_ALIGN_TOP_LEFT, 2, title_bar_h + 2);
  lv_obj_set_style_bg_opa(s_scroll_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_scroll_cont, 0, 0);
  lv_obj_set_style_pad_left(s_scroll_cont, left_margin, 0);
  lv_obj_set_style_pad_top(s_scroll_cont, 4, 0);
  lv_obj_set_style_pad_right(s_scroll_cont, left_margin, 0);
  lv_obj_set_style_pad_bottom(s_scroll_cont, 8, 0);
  lv_obj_set_scroll_dir(s_scroll_cont, LV_DIR_VER);
  lv_obj_add_flag(s_scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_scroll_cont, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(s_scroll_cont, 0, LV_STATE_FOCUSED);

  lv_obj_t *label = lv_label_create(s_scroll_cont);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_width(label, disp_w - (left_margin * 2) - 8);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_update_layout(s_scroll_cont);
  lv_obj_scroll_to(s_scroll_cont, 0, 0, LV_ANIM_OFF);

  inspect_scene_rebind_input();
  return screen;
}

lv_obj_t *menu_page_inspect_scene_create(void) {
  scene_t *scene = scene_get_current();
  uint8_t scene_index = scene_get_current_index();

  if (!scene_inspect_build(scene, scene_index, s_inspect_text, sizeof(s_inspect_text))) {
    ESP_LOGW(TAG, "Inspect text truncated");
  }

  g_inspect_active = true;
  subscribe_scene_changed();

  ESP_LOGD(TAG, "Inspect scene page created");
  return create_inspect_screen(s_inspect_text, scene_index);
}

void menu_page_inspect_scene_cleanup(void) {
  g_inspect_active = false;
  s_scroll_cont = NULL;
  unsubscribe_scene_changed();

  lv_group_t *group = menu_get_group();
  if (group) lv_group_set_editing(group, false);

  ESP_LOGD(TAG, "Inspect scene cleanup");
}
