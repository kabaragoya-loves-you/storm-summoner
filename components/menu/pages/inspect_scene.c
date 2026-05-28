#include "menu.h"
#include "menu_pages.h"
#include "scene_inspect.h"
#include "scene.h"
#include "inspect_ui.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"
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
static int32_t s_inspect_line_step_px = 0;
static int32_t s_inspect_scroll_pad_top_px = 4;

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

void inspect_scene_invalidate_scroll(void) {
  s_scroll_cont = NULL;
}

static bool inspect_scroll_target_valid(void) {
  return g_inspect_active && s_scroll_cont && lv_obj_is_valid(s_scroll_cont);
}

void inspect_scene_rebind_input(void) {
  if (!inspect_scroll_target_valid()) return;

  lv_group_t *group = menu_get_group();
  if (!group) return;

  lv_group_remove_all_objs(group);
  lv_group_add_obj(group, s_scroll_cont);
  lv_group_focus_obj(s_scroll_cont);
  lv_group_set_editing(group, true);
  ui_attach_encoder_to_menu();
}

bool inspect_scene_jog_scroll(uint8_t pad_id) {
  if (!inspect_scroll_target_valid()) return false;
  if (pad_id != INSPECT_PAD_GAMMA_LOGICAL && pad_id != INSPECT_PAD_ALPHA_LOGICAL) return false;

  int32_t step = s_inspect_line_step_px;
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
  if (y == 0) target += s_inspect_scroll_pad_top_px;

  int32_t before = y;
  lv_obj_scroll_to_y(s_scroll_cont, target, LV_ANIM_OFF);
  return lv_obj_get_scroll_y(s_scroll_cont) != before;
}

static lv_obj_t *create_inspect_screen(const char *text, uint8_t scene_index) {
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_t *scroll_cont = NULL;
  inspect_ui_create(screen, text, scene_index, &scroll_cont);
  s_scroll_cont = scroll_cont;

  if (scroll_cont && lv_obj_is_valid(scroll_cont)) {
    lv_obj_t *label = lv_obj_get_child(scroll_cont, 0);
    inspect_ui_cache_scroll_metrics(label, scroll_cont,
      &s_inspect_line_step_px, &s_inspect_scroll_pad_top_px);
  }

  inspect_scene_rebind_input();
  return screen;
}

lv_obj_t *menu_page_inspect_scene_create(void) {
  inspect_scene_invalidate_scroll();

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
  inspect_scene_invalidate_scroll();
  s_inspect_line_step_px = 0;
  unsubscribe_scene_changed();

  lv_group_t *group = menu_get_group();
  if (group) lv_group_set_editing(group, false);

  ESP_LOGD(TAG, "Inspect scene cleanup");
}
