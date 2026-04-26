#include "lvgl.h"
#include "ui.h"
#include "event_bus.h"
#include "action_summary.h"
#include "esp_log.h"
#include "misc/lv_async.h"

#define TAG "KHYRON"

// Screen and widget references
static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_label = NULL;
static lv_timer_t *g_hide_timer = NULL;

// Module active flag
static volatile bool g_module_active = false;

// Text buffer for display
static char g_text_buf[256];

// Hide timeout in ms
#define HIDE_TIMEOUT_MS 1000

// Forward declarations
static void khyron_draw_deferred_cb(lv_timer_t *timer);
static void action_executed_handler(const event_t *event, void *context);

// Timer callback to hide the label
static void hide_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (g_label) {
    lv_label_set_text(g_label, "");
  }
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

// Map source_type and source_index to summary_input_t
static summary_input_t map_source_to_input(uint8_t source_type, uint8_t source_index) {
  switch (source_type) {
    case 0:  // pad
      if (source_index <= 11) return (summary_input_t)(SUMMARY_INPUT_PAD_0 + source_index);
      break;
    case 1:  // button
      if (source_index == 0) return SUMMARY_INPUT_BUTTON_L;
      if (source_index == 1) return SUMMARY_INPUT_BUTTON_R;
      if (source_index == 2) return SUMMARY_INPUT_BUTTON_BOTH;
      break;
    case 2:  // bump
      return SUMMARY_INPUT_BUMP;
    case 3:  // on_load
      return SUMMARY_INPUT_ON_LOAD;
    case 4:  // on_play
      return SUMMARY_INPUT_ON_PLAY;
    case 5:  // expr_switch
      return SUMMARY_INPUT_EXPRESSION;
    default:
      break;
  }
  return SUMMARY_INPUT_UNKNOWN;
}

// Async callback to update display from event context
typedef struct {
  action_summary_t summary;
} khyron_update_t;

static khyron_update_t g_pending_update;

static void update_display_async(void *user_data) {
  (void)user_data;
  if (!g_module_active || !g_label) return;

  // Format with cyan input name
  action_summary_format_display(&g_pending_update.summary, g_text_buf,
    sizeof(g_text_buf), 0x00FFFF);

  lv_label_set_recolor(g_label, true);
  lv_label_set_text(g_label, g_text_buf);

  // Start/reset hide timer
  start_hide_timer();
}

static void action_executed_handler(const event_t *event, void *context) {
  (void)context;
  if (!g_module_active || !event) return;

  // Don't show actions while in programming mode
  if (ui_is_in_programming_mode()) return;

  // Build the summary
  action_summary_init(&g_pending_update.summary);

  // Determine input source
  summary_input_t input = map_source_to_input(
    event->data.action_executed.source_type,
    event->data.action_executed.source_index);

  // For unknown sources, try to use the action type to create a meaningful name
  if (input == SUMMARY_INPUT_UNKNOWN) {
    snprintf(g_pending_update.summary.input_name,
      sizeof(g_pending_update.summary.input_name), "Action");
  } else {
    action_summary_set_input(&g_pending_update.summary, input, true);
  }

  // Create a minimal action struct for formatting
  // (We only have limited info from the event)
  action_t action = {0};
  action.type = (action_type_t)event->data.action_executed.action_type;

  if (action.type == ACTION_CONTROL_CHANGE ||
      action.type == ACTION_CONTROL_HOLD ||
      action.type == ACTION_CONTROL_CYCLE) {
    action.params.control.num_ccs = 1;
    action.params.control.cc_numbers[0] = event->data.action_executed.cc_number;
    action.params.control.values[0] = event->data.action_executed.cc_value;
    if (action.type == ACTION_CONTROL_HOLD) {
      action.params.control.values2[0] = event->data.action_executed.cc_value2;
    }
  } else if (action.type == ACTION_NOTE) {
    action.params.note.note = event->data.action_executed.note;
    action.params.note.velocity = event->data.action_executed.velocity;
  }

  // Format the action summary
  action_format_summary(&action, &g_pending_update.summary, scene_get_current_index());

  // Schedule async update
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

  // Create screen with black background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, disp_width, disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

  // Create centered label for action display
  g_label = lv_label_create(g_screen);
  lv_obj_set_width(g_label, disp_width - 20);
  lv_obj_align(g_label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(g_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(g_label, lv_color_make(0, 255, 0), 0);
  lv_label_set_recolor(g_label, true);
  lv_label_set_text(g_label, "");

  // Subscribe to action executed events
  event_bus_subscribe(EVENT_ACTION_EXECUTED, action_executed_handler, NULL);

  // Mark module as active
  g_module_active = true;

  ESP_LOGI(TAG, "Khyron module created");

  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(khyron, khyron_draw_deferred_cb)

static void khyron_teardown(void) {
  // Mark module as inactive
  g_module_active = false;

  // Unsubscribe from events
  event_bus_unsubscribe(EVENT_ACTION_EXECUTED, action_executed_handler);

  // Clean up hide timer
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
