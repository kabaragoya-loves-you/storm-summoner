#include "lvgl.h"
#include "ui.h"
#include "event_bus.h"
#include "action.h"
#include "action_summary.h"
#include "esp_log.h"
#include "misc/lv_async.h"
#include <string.h>

#define TAG "KHYRON"

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_label = NULL;
static lv_timer_t *g_hide_timer = NULL;

static volatile bool g_module_active = false;

static char g_text_buf[512];

#define HIDE_TIMEOUT_MS 1500
#define INPUT_COLOR 0x00FFFF

typedef struct {
  action_trigger_source_t source;
  uint8_t scene_index;
  action_t action;
} khyron_pending_t;

static khyron_pending_t g_pending;

static void khyron_draw_deferred_cb(lv_timer_t *timer);
static void action_executed_handler(const event_t *event, void *context);

static void hide_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (g_label) lv_label_set_text(g_label, "");
  g_hide_timer = NULL;
}

static void start_hide_timer(void) {
  if (g_hide_timer) {
    lv_timer_reset(g_hide_timer);
  } else {
    g_hide_timer = lv_timer_create(hide_timer_cb, HIDE_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(g_hide_timer, 1);
  }
}

static void format_source_label(const action_trigger_source_t *src, char *buf, size_t len) {
  if (!src || !buf || len == 0) return;
  buf[0] = '\0';

  if (src->type == ACTION_SOURCE_PAD && src->index <= 11) {
    action_summary_t summary;
    action_summary_init(&summary);
    action_summary_set_input(&summary,
      (summary_input_t)(SUMMARY_INPUT_PAD_0 + src->index), true);
    snprintf(buf, len, "%s", summary.input_name);
    return;
  }

  switch (src->type) {
    case ACTION_SOURCE_BUTTON:
      if (src->index == 0) snprintf(buf, len, "Left Button");
      else if (src->index == 1) snprintf(buf, len, "Right Button");
      else snprintf(buf, len, "Both Buttons");
      break;
    case ACTION_SOURCE_BUMP:
      snprintf(buf, len, "Bump");
      break;
    case ACTION_SOURCE_FOOTSWITCH:
      if (src->index == 0) snprintf(buf, len, "Sustain");
      else if (src->index == 1) snprintf(buf, len, "Sostenuto");
      else snprintf(buf, len, "Expr Switch");
      break;
    case ACTION_SOURCE_CC_INPUT:
      snprintf(buf, len, "CC %u In", (unsigned)src->index);
      break;
    case ACTION_SOURCE_SCHEDULED:
      snprintf(buf, len, "Scheduled");
      break;
    case ACTION_SOURCE_ON_LOAD:
      snprintf(buf, len, "On Load");
      break;
    case ACTION_SOURCE_ON_PLAY:
      snprintf(buf, len, "On Play");
      break;
    case ACTION_SOURCE_CV:
      snprintf(buf, len, "CV Trigger");
      break;
    default:
      snprintf(buf, len, "Action");
      break;
  }
}

static void update_display_async(void *user_data) {
  (void)user_data;
  if (!g_module_active || !g_label) return;

  char src_label[32];
  format_source_label(&g_pending.source, src_label, sizeof(src_label));

  const char *family = action_summary_inspect_family_name(g_pending.action.type);
  char body[384];
  body[0] = '\0';
  action_summary_format_inspect_action_body(&g_pending.action, g_pending.scene_index,
    body, sizeof(body));

  const char *body_start = body;
  while (*body_start == '\n') body_start++;

  uint8_t r = (INPUT_COLOR >> 16) & 0xFF;
  uint8_t g = (INPUT_COLOR >> 8) & 0xFF;
  uint8_t b = INPUT_COLOR & 0xFF;

  if (*body_start) {
    snprintf(g_text_buf, sizeof(g_text_buf), "#%02X%02X%02X %s#\n%s\n%s",
      r, g, b, src_label, family, body_start);
  } else {
    snprintf(g_text_buf, sizeof(g_text_buf), "#%02X%02X%02X %s#\n%s",
      r, g, b, src_label, family);
  }

  lv_label_set_recolor(g_label, true);
  lv_label_set_text(g_label, g_text_buf);
  start_hide_timer();
}

static void action_executed_handler(const event_t *event, void *context) {
  (void)event;
  (void)context;
  if (!g_module_active) return;
  if (ui_is_in_programming_mode()) return;

  if (!action_get_last_executed(&g_pending.source, &g_pending.scene_index, &g_pending.action))
    return;

  lv_async_call(update_display_async, NULL);
}

static void khyron_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }

  lv_display_t *disp = lv_display_get_default();
  uint16_t disp_width = lv_display_get_horizontal_resolution(disp);
  uint16_t disp_height = lv_display_get_vertical_resolution(disp);

  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, disp_width, disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

  g_label = lv_label_create(g_screen);
  lv_obj_set_width(g_label, disp_width - 20);
  lv_obj_align(g_label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(g_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_label, lv_color_make(0, 255, 0), 0);
  lv_label_set_recolor(g_label, true);
  lv_label_set_text(g_label, "");

  event_bus_subscribe(EVENT_ACTION_EXECUTED, action_executed_handler, NULL);
  g_module_active = true;

  ESP_LOGI(TAG, "Khyron module created");

  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(khyron, khyron_draw_deferred_cb)

static void khyron_teardown(void) {
  g_module_active = false;
  event_bus_unsubscribe(EVENT_ACTION_EXECUTED, action_executed_handler);

  if (g_hide_timer) {
    lv_timer_delete(g_hide_timer);
    g_hide_timer = NULL;
  }

  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_label = NULL;
  }

  ESP_LOGI(TAG, "Khyron module teardown");
}

static void khyron_init(void) {
  ESP_LOGD(TAG, "Khyron module init");
}

ui_draw_module_t khyron_module = {
  .draw_func = khyron_draw,
  .teardown_func = khyron_teardown,
  .init_func = khyron_init,
  .name = "khyron",
  .title = "Khyron"
};
