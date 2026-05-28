#include "inspect_overlay.h"
#include "inspect_ui.h"
#include "inspect_config.h"
#include "scene_inspect.h"
#include "scene.h"
#include "ui.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "inspect_overlay";

#define INSPECT_TEXT_BUF_SIZE 2048
#define SCROLL_TIMER_PERIOD_MS 50

static bool s_want_active = false;
static bool s_overlay_active = false;
static char s_inspect_text[INSPECT_TEXT_BUF_SIZE];
static lv_obj_t *s_root = NULL;
static lv_obj_t *s_scroll_cont = NULL;
static lv_timer_t *s_scroll_timer = NULL;
static int32_t s_scroll_direction = 1;

static bool scroll_target_valid(void) {
  return s_overlay_active && s_scroll_cont && lv_obj_is_valid(s_scroll_cont);
}

static void invalidate_scroll_target(void) {
  s_scroll_cont = NULL;
}

static void scroll_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (!scroll_target_valid()) return;

  int32_t step = inspect_config_scroll_step_px();
  if (step <= 0) return;

  lv_obj_update_layout(s_scroll_cont);
  int32_t y = lv_obj_get_scroll_y(s_scroll_cont);
  int32_t scroll_bottom = lv_obj_get_scroll_bottom(s_scroll_cont);

  if (inspect_config_get_scroll_mode() == INSPECT_SCROLL_MODE_LOOP_DOWN) {
    if (scroll_bottom <= 0) {
      lv_obj_scroll_to_y(s_scroll_cont, 0, LV_ANIM_OFF);
      return;
    }
    lv_obj_scroll_to_y(s_scroll_cont, y + step, LV_ANIM_OFF);
    return;
  }

  // Ping-pong: down until bottom, then up until top.
  if (s_scroll_direction > 0) {
    if (scroll_bottom <= 0) {
      s_scroll_direction = -1;
      if (y > 0) lv_obj_scroll_to_y(s_scroll_cont, y - step, LV_ANIM_OFF);
      return;
    }
    lv_obj_scroll_to_y(s_scroll_cont, y + step, LV_ANIM_OFF);
    return;
  }

  if (y <= 0) {
    s_scroll_direction = 1;
    return;
  }
  lv_obj_scroll_to_y(s_scroll_cont, y - step, LV_ANIM_OFF);
}

static void destroy_overlay_widgets(void) {
  if (s_scroll_timer) {
    lv_timer_delete(s_scroll_timer);
    s_scroll_timer = NULL;
  }

  invalidate_scroll_target();

  if (s_root && lv_obj_is_valid(s_root)) {
    lv_obj_delete(s_root);
  }
  s_root = NULL;
  s_overlay_active = false;
  s_scroll_direction = 1;
}

static void overlay_show_async(void *param) {
  (void)param;
  if (!s_want_active) return;
  if (!ui_is_in_performance_mode()) {
    s_want_active = false;
    return;
  }
  if (s_overlay_active) return;

  scene_t *scene = scene_get_current();
  uint8_t scene_index = scene_get_current_index();
  if (!scene_inspect_build(scene, scene_index, s_inspect_text, sizeof(s_inspect_text))) {
    ESP_LOGW(TAG, "Inspect text truncated");
  }

  lv_display_t *disp = lv_display_get_default();
  if (!disp) {
    s_want_active = false;
    return;
  }

  s_root = lv_obj_create(lv_display_get_layer_top(disp));
  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *scroll_cont = NULL;
  inspect_ui_create(s_root, s_inspect_text, scene_index, &scroll_cont);
  s_scroll_cont = scroll_cont;

  s_overlay_active = true;
  s_scroll_direction = 1;

  s_scroll_timer = lv_timer_create(scroll_timer_cb, SCROLL_TIMER_PERIOD_MS, NULL);
  if (s_scroll_timer) lv_timer_set_repeat_count(s_scroll_timer, -1);

  ESP_LOGD(TAG, "Performance inspect overlay shown");
}

static void overlay_hide_async(void *param) {
  (void)param;
  s_want_active = false;
  destroy_overlay_widgets();
  ESP_LOGD(TAG, "Performance inspect overlay hidden");
}

bool inspect_overlay_is_active(void) {
  return s_overlay_active || s_want_active;
}

void inspect_overlay_show(void) {
  if (!ui_is_in_performance_mode()) return;
  s_want_active = true;
  lv_async_call(overlay_show_async, NULL);
}

void inspect_overlay_hide(void) {
  s_want_active = false;
  lv_async_call(overlay_hide_async, NULL);
}
