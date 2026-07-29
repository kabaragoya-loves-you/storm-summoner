#include "toast_overlay.h"
#include "esp_log.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "toast_overlay";

#define TOAST_TIMEOUT_MS 1500
#define TOAST_MSG_MAX 64

static char s_pending_msg[TOAST_MSG_MAX];
static lv_obj_t *s_root = NULL;
static lv_timer_t *s_hide_timer = NULL;

static void destroy_toast(void) {
  if (s_hide_timer) {
    lv_timer_delete(s_hide_timer);
    s_hide_timer = NULL;
  }
  if (s_root && lv_obj_is_valid(s_root))
    lv_obj_delete(s_root);
  s_root = NULL;
}

static void hide_timer_cb(lv_timer_t *timer) {
  (void)timer;
  destroy_toast();
}

static void toast_show_async(void *param) {
  (void)param;

  destroy_toast();

  lv_display_t *disp = lv_display_get_default();
  if (!disp) return;

  s_root = lv_obj_create(lv_display_get_layer_top(disp));
  if (!s_root) return;

  lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
  lv_obj_set_style_border_width(s_root, 0, 0);
  lv_obj_set_style_pad_all(s_root, 8, 0);
  lv_obj_set_style_radius(s_root, 6, 0);
  lv_obj_set_width(s_root, LV_PCT(80));
  lv_obj_align(s_root, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *label = lv_label_create(s_root);
  lv_label_set_text(label, s_pending_msg);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  lv_obj_center(label);

  s_hide_timer = lv_timer_create(hide_timer_cb, TOAST_TIMEOUT_MS, NULL);
  if (s_hide_timer) lv_timer_set_repeat_count(s_hide_timer, 1);

  ESP_LOGD(TAG, "Toast: %s", s_pending_msg);
}

void toast_overlay_show(const char *message) {
  if (!message || !message[0]) return;
  strncpy(s_pending_msg, message, sizeof(s_pending_msg) - 1);
  s_pending_msg[sizeof(s_pending_msg) - 1] = '\0';
  lv_async_call(toast_show_async, NULL);
}
