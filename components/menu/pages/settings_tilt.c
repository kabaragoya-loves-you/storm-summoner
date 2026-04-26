#include "menu.h"
#include "menu_pages.h"
#include "tilt.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "MENU_SETTINGS_TILT"

lv_obj_t* menu_page_settings_tilt_create(void);
static lv_obj_t* calibration_page_create(void);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_TILT_ITEMS 12
static menu_item_t s_items[MAX_TILT_ITEMS];

static char s_calibrate_label[LABEL_BUFFER_SETS][32];
static char s_forgive_label[LABEL_BUFFER_SETS][32];
static char s_mid_w_label[LABEL_BUFFER_SETS][32];
static char s_deadzone_label[LABEL_BUFFER_SETS][32];
static char s_rate_label[LABEL_BUFFER_SETS][32];
static char s_inv_x_label[LABEL_BUFFER_SETS][32];
static char s_inv_y_label[LABEL_BUFFER_SETS][32];
static char s_note_off_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// ============================================================================
// 5-step Calibration Wizard
// ============================================================================

static const char* s_cal_step_instructions[TILT_CAL_NUM_STEPS + 1] = {
  "Place device\nFLAT at rest\nand hold...",
  "Tilt fully\nLEFT\nand hold...",
  "Tilt fully\nRIGHT\nand hold...",
  "Tilt fully\nFORWARD\nand hold...",
  "Tilt fully\nBACK\nand hold...",
  "Calibration\nComplete!\n\nPress any button\nto continue"
};

#define CAL_SAMPLE_INTERVAL_MS 50
#define CAL_STABILITY_WINDOW   20
#define CAL_STABILITY_VAR      40.0f
#define CAL_STABLE_DURATION    20

static int s_cal_step = 0;
static float s_cal_samples_x[CAL_STABILITY_WINDOW];
static float s_cal_samples_y[CAL_STABILITY_WINDOW];
static uint8_t s_cal_sample_index = 0;
static uint8_t s_cal_sample_count = 0;
static uint8_t s_cal_stable_count = 0;
static bool    s_cal_error = false;
static lv_timer_t* s_cal_timer = NULL;
static lv_obj_t* s_cal_instruction_label = NULL;
static lv_obj_t* s_cal_value_label = NULL;
static lv_obj_t* s_cal_screen = NULL;
static lv_obj_t* s_cal_done_btn = NULL;

static void calibration_cleanup(void) {
  if (s_cal_timer) {
    lv_timer_delete(s_cal_timer);
    s_cal_timer = NULL;
  }
  s_cal_instruction_label = NULL;
  s_cal_value_label = NULL;
  s_cal_screen = NULL;
  s_cal_done_btn = NULL;
}

static bool is_stable(const float* samples) {
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) return false;
  float sum = 0.0f;
  for (int i = 0; i < CAL_STABILITY_WINDOW; i++) sum += samples[i];
  float mean = sum / CAL_STABILITY_WINDOW;
  float var = 0.0f;
  for (int i = 0; i < CAL_STABILITY_WINDOW; i++) {
    float d = samples[i] - mean;
    var += d * d;
  }
  var /= CAL_STABILITY_WINDOW;
  return var < (CAL_STABILITY_VAR * CAL_STABILITY_VAR);
}

static void advance_calibration_step(void) {
  ESP_LOGI(TAG, "Tilt cal step %d stable; capturing", s_cal_step);
  esp_err_t ret = tilt_cal_capture((tilt_cal_step_t)s_cal_step);
  if (ret != ESP_OK) {
    s_cal_error = true;
    if (s_cal_value_label) lv_label_set_text(s_cal_value_label, "Capture failed");
    return;
  }

  s_cal_step++;
  s_cal_stable_count = 0;
  s_cal_sample_count = 0;
  s_cal_sample_index = 0;

  if (s_cal_step >= TILT_CAL_NUM_STEPS) {
    if (s_cal_timer) {
      lv_timer_delete(s_cal_timer);
      s_cal_timer = NULL;
    }
    esp_err_t commit = tilt_cal_commit();
    if (commit == ESP_OK) {
      if (s_cal_instruction_label) {
        lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[TILT_CAL_NUM_STEPS]);
      }
      if (s_cal_value_label) lv_label_set_text(s_cal_value_label, "Saved.");
    } else {
      if (s_cal_instruction_label) {
        lv_label_set_text(s_cal_instruction_label, "Insufficient\nrange!\n\nTry again.");
      }
      tilt_cal_abort();
    }
    return;
  }

  if (s_cal_instruction_label) {
    lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[s_cal_step]);
  }
}

static void calibration_timer_cb(lv_timer_t* timer) {
  (void)timer;
  if (s_cal_error) return;
  if (s_cal_step >= TILT_CAL_NUM_STEPS) return;

  int16_t raw_x = 0, raw_y = 0;
  tilt_get_last_xy(&raw_x, &raw_y);

  s_cal_samples_x[s_cal_sample_index] = (float)raw_x;
  s_cal_samples_y[s_cal_sample_index] = (float)raw_y;
  s_cal_sample_index = (s_cal_sample_index + 1) % CAL_STABILITY_WINDOW;
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) s_cal_sample_count++;

  if (s_cal_value_label) {
    static char buf[48];
    snprintf(buf, sizeof(buf), "x=%d y=%d", (int)raw_x, (int)raw_y);
    lv_label_set_text(s_cal_value_label, buf);
  }

  // Use the axis most relevant to the current step for stability; the center
  // step considers both axes.
  bool stable = false;
  switch ((tilt_cal_step_t)s_cal_step) {
    case TILT_CAL_CENTER:
      stable = is_stable(s_cal_samples_x) && is_stable(s_cal_samples_y);
      break;
    case TILT_CAL_LEFT:
    case TILT_CAL_RIGHT:
      stable = is_stable(s_cal_samples_x);
      break;
    case TILT_CAL_FORWARD:
    case TILT_CAL_BACK:
      stable = is_stable(s_cal_samples_y);
      break;
    default:
      break;
  }
  if (stable) {
    s_cal_stable_count++;
    if (s_cal_stable_count >= CAL_STABLE_DURATION) advance_calibration_step();
  } else {
    s_cal_stable_count = 0;
  }
}

static bool calibration_handle_back(void) {
  ESP_LOGI(TAG, "Tilt calibration cancelled/completed");
  if (s_cal_step < TILT_CAL_NUM_STEPS) tilt_cal_abort();
  calibration_cleanup();
  menu_set_custom_back_handler(NULL);
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
  return true;
}

static void calibration_click_cb(lv_event_t* e) {
  (void)e;
  calibration_handle_back();
}

static lv_obj_t* calibration_page_create(void) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();

  s_cal_step = 0;
  s_cal_sample_count = 0;
  s_cal_sample_index = 0;
  s_cal_stable_count = 0;
  s_cal_error = false;
  // tilt_cal_begin() sets s_cal_in_progress, which keeps the unified task
  // polling (via tilt_poll_active()) and updating s_last_sample_x/y for the
  // wizard UI. We deliberately don't touch per-axis enable: it gates MIDI
  // event emission, and we don't want spurious tilt events while the user
  // is moving the device through the calibration poses.
  tilt_cal_begin();

  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  s_cal_screen = screen;

  const int title_bar_h = 22;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, "Tilt Calibration");
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);

  lv_obj_t* content = lv_obj_create(screen);
  lv_obj_set_size(content, disp_w, disp_h - title_bar_h);
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 10, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  s_cal_instruction_label = lv_label_create(content);
  lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[0]);
  lv_obj_set_style_text_color(s_cal_instruction_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cal_instruction_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_cal_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_cal_instruction_label, LV_ALIGN_CENTER, 0, -15);

  s_cal_value_label = lv_label_create(content);
  lv_label_set_text(s_cal_value_label, "x=0 y=0");
  lv_obj_set_style_text_color(s_cal_value_label, lv_color_make(150, 150, 150), 0);
  lv_obj_set_style_text_font(s_cal_value_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_cal_value_label, LV_ALIGN_BOTTOM_MID, 0, -5);

  s_cal_done_btn = lv_obj_create(content);
  lv_obj_set_size(s_cal_done_btn, disp_w - 20, disp_h - title_bar_h - 20);
  lv_obj_center(s_cal_done_btn);
  lv_obj_set_style_bg_opa(s_cal_done_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_cal_done_btn, 0, 0);
  lv_obj_add_flag(s_cal_done_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_cal_done_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_cal_done_btn, calibration_click_cb, LV_EVENT_CLICKED, NULL);

  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, s_cal_done_btn);
    lv_group_focus_obj(s_cal_done_btn);
  }

  menu_set_custom_back_handler(calibration_handle_back);

  s_cal_timer = lv_timer_create(calibration_timer_cb, CAL_SAMPLE_INTERVAL_MS, NULL);

  ESP_LOGI(TAG, "Tilt calibration wizard started");
  return screen;
}

static void nav_to_calibrate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tilt Cal", calibration_page_create);
}

// ============================================================================
// Forgive Middle toggle
// ============================================================================

static void forgive_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  tilt_set_forgive_middle(selected_index == 1);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static lv_obj_t* forgive_roller_create(void) {
  static const char* options = "Off\nOn";
  uint32_t current = tilt_get_forgive_middle() ? 1 : 0;
  return menu_create_roller_page("Forgive", options, current, forgive_confirm_cb, NULL);
}

static void nav_to_forgive(void* user_data) {
  (void)user_data;
  menu_navigate_to("Forgive", forgive_roller_create);
}

// ============================================================================
// Middle Width slider (1..30)
// ============================================================================

static void mid_w_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  tilt_set_middle_width((uint8_t)(selected_index + 1));
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static lv_obj_t* mid_w_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 1; v <= 30; v++) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%d%s", v, v < 30 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }
  uint8_t current = tilt_get_middle_width();
  if (current < 1) current = 1;
  if (current > 30) current = 30;
  return menu_create_roller_page("Middle Width", options, current - 1, mid_w_confirm_cb, NULL);
}

static void nav_to_mid_w(void* user_data) {
  (void)user_data;
  menu_navigate_to("Middle Width", mid_w_roller_create);
}

// ============================================================================
// Deadzone roller (0..10)
// ============================================================================

static void deadzone_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  tilt_set_deadzone((uint8_t)selected_index);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static lv_obj_t* deadzone_roller_create(void) {
  static const char* options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
  uint8_t current = tilt_get_deadzone();
  if (current > 10) current = 10;
  return menu_create_roller_page("Deadzone", options, current, deadzone_confirm_cb, NULL);
}

static void nav_to_deadzone(void* user_data) {
  (void)user_data;
  menu_navigate_to("Deadzone", deadzone_roller_create);
}

// ============================================================================
// Rate roller (10..100 Hz, step 5)
// ============================================================================

static void rate_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  uint8_t hz = (uint8_t)(10 + selected_index * 5);
  tilt_set_rate_hz(hz);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static lv_obj_t* rate_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 10; v <= 100; v += 5) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d Hz%s", v, v < 100 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }
  uint8_t current = tilt_get_rate_hz();
  if (current < 10) current = 10;
  if (current > 100) current = 100;
  uint32_t idx = (current - 10) / 5;
  return menu_create_roller_page("Rate", options, idx, rate_confirm_cb, NULL);
}

static void nav_to_rate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rate", rate_roller_create);
}

// ============================================================================
// X / Y Invert (global axis polarity)
// ============================================================================

static void invert_axis_confirm(tilt_axis_t axis, uint32_t selected_index) {
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  tilt_set_axis_inverted(axis, selected_index == 1);
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static void invert_x_confirm_cb(uint32_t i, void* u) { (void)u; invert_axis_confirm(TILT_AXIS_X, i); }
static void invert_y_confirm_cb(uint32_t i, void* u) { (void)u; invert_axis_confirm(TILT_AXIS_Y, i); }

static lv_obj_t* invert_x_roller_create(void) {
  uint32_t current = tilt_get_axis_inverted(TILT_AXIS_X) ? 1 : 0;
  return menu_create_roller_page("Invert X", "Normal\nInverted", current,
    invert_x_confirm_cb, NULL);
}

static lv_obj_t* invert_y_roller_create(void) {
  uint32_t current = tilt_get_axis_inverted(TILT_AXIS_Y) ? 1 : 0;
  return menu_create_roller_page("Invert Y", "Normal\nInverted", current,
    invert_y_confirm_cb, NULL);
}

static void nav_to_invert_x(void* u) { (void)u; menu_navigate_to("Invert X", invert_x_roller_create); }
static void nav_to_invert_y(void* u) { (void)u; menu_navigate_to("Invert Y", invert_y_roller_create); }

// ============================================================================
// Note Off (auto note-off while parked in the forgive middle zone)
// ============================================================================

static void note_off_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  if (selected_index < TILT_NOTE_OFF_NUM_MODES) {
    tilt_set_note_off_mode((tilt_note_off_mode_t)selected_index);
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Tilt", menu_page_settings_tilt_create);
}

static lv_obj_t* note_off_roller_create(void) {
  // Build newline-joined option list from the canonical labels.
  static char options[256];
  options[0] = '\0';
  for (int i = 0; i < TILT_NOTE_OFF_NUM_MODES; i++) {
    if (i > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
    strncat(options, tilt_note_off_mode_label((tilt_note_off_mode_t)i),
      sizeof(options) - strlen(options) - 1);
  }
  uint32_t current = (uint32_t)tilt_get_note_off_mode();
  return menu_create_roller_page("Note Off", options, current, note_off_confirm_cb, NULL);
}

static void nav_to_note_off(void* u) { (void)u; menu_navigate_to("Note Off", note_off_roller_create); }

// ============================================================================
// Main Settings Tilt Page
// ============================================================================

lv_obj_t* menu_page_settings_tilt_create(void) {
  ESP_LOGI(TAG, "Creating Settings Tilt page");

  int buf = get_next_buffer_set();
  int idx = 0;

  snprintf(s_calibrate_label[buf], sizeof(s_calibrate_label[buf]),
    "Calibrate\n%s", tilt_is_calibrated() ? "OK" : "Needed");
  s_items[idx++] = (menu_item_t){s_calibrate_label[buf], nav_to_calibrate, NULL, true};

  bool forgive = tilt_get_forgive_middle();
  snprintf(s_forgive_label[buf], sizeof(s_forgive_label[buf]),
    "Forgive Mid\n%s", forgive ? "On" : "Off");
  s_items[idx++] = (menu_item_t){s_forgive_label[buf], nav_to_forgive, NULL, true};

  if (forgive) {
    snprintf(s_mid_w_label[buf], sizeof(s_mid_w_label[buf]),
      "Middle Width\n%u", (unsigned)tilt_get_middle_width());
    s_items[idx++] = (menu_item_t){s_mid_w_label[buf], nav_to_mid_w, NULL, true};

    snprintf(s_note_off_label[buf], sizeof(s_note_off_label[buf]),
      "Note Off\n%s", tilt_note_off_mode_label(tilt_get_note_off_mode()));
    s_items[idx++] = (menu_item_t){s_note_off_label[buf], nav_to_note_off, NULL, true};
  }

  snprintf(s_deadzone_label[buf], sizeof(s_deadzone_label[buf]),
    "Deadzone\n%u", (unsigned)tilt_get_deadzone());
  s_items[idx++] = (menu_item_t){s_deadzone_label[buf], nav_to_deadzone, NULL, true};

  snprintf(s_rate_label[buf], sizeof(s_rate_label[buf]),
    "Rate\n%u Hz", (unsigned)tilt_get_rate_hz());
  s_items[idx++] = (menu_item_t){s_rate_label[buf], nav_to_rate, NULL, true};

  snprintf(s_inv_x_label[buf], sizeof(s_inv_x_label[buf]),
    "Invert X\n%s", tilt_get_axis_inverted(TILT_AXIS_X) ? "Inverted" : "Normal");
  s_items[idx++] = (menu_item_t){s_inv_x_label[buf], nav_to_invert_x, NULL, true};

  snprintf(s_inv_y_label[buf], sizeof(s_inv_y_label[buf]),
    "Invert Y\n%s", tilt_get_axis_inverted(TILT_AXIS_Y) ? "Inverted" : "Normal");
  s_items[idx++] = (menu_item_t){s_inv_y_label[buf], nav_to_invert_y, NULL, true};

  return menu_create_page_2line("Tilt", s_items, idx);
}
