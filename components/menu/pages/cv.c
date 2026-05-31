#include "menu.h"
#include "menu_pages.h"
#include "cv.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

#define TAG "MENU_SETTINGS_CV"

// Forward declarations
lv_obj_t* menu_page_cv_create(void);
static lv_obj_t* calibration_page_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_CV_ITEMS 5
static menu_item_t s_cv_items[MAX_CV_ITEMS];

static char s_range_label[LABEL_BUFFER_SETS][40];
static char s_calibrate_label[LABEL_BUFFER_SETS][40];
static char s_pitch_std_label[LABEL_BUFFER_SETS][40];
static char s_deadzone_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static const char* range_to_string(cv_range_t range) {
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: return "+/-10V";
    case CV_RANGE_10V: return "0-10V";
    case CV_RANGE_BIPOLAR_5V: return "+/-5V";
    case CV_RANGE_5V: return "0-5V";
    case CV_RANGE_3V3: return "0-3.3V";
    default: return "Unknown";
  }
}

static const char* range_to_short_string(cv_range_t range) {
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: return "+/-10V";
    case CV_RANGE_10V: return "0-10V";
    case CV_RANGE_BIPOLAR_5V: return "+/-5V";
    case CV_RANGE_5V: return "0-5V";
    case CV_RANGE_3V3: return "0-3.3V";
    default: return "?";
  }
}

static const char* pitch_std_to_string(cv_pitch_standard_t std) {
  switch (std) {
    case CV_PITCH_1V_OCTAVE_C0: return "1V/Oct (C0@0V)";
    case CV_PITCH_1V_OCTAVE_C2: return "1V/Oct (C2@0V)";
    case CV_PITCH_HZ_V: return "Hz/V (Buchla)";
    default: return "Unknown";
  }
}

// ============================================================================
// Voltage Range Roller
// ============================================================================

static void range_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  cv_range_t ranges[] = {
    CV_RANGE_10V,
    CV_RANGE_BIPOLAR_10V,
    CV_RANGE_5V,
    CV_RANGE_BIPOLAR_5V,
    CV_RANGE_3V3
  };
  
  if (selected_index < 5) {
    cv_set_range(ranges[selected_index]);
    ESP_LOGI(TAG, "CV range set to: %s", range_to_string(ranges[selected_index]));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_create);
}

static lv_obj_t* range_roller_create(void) {
  static const char* options = "0-10V\n+/-10V\n0-5V\n+/-5V\n0-3.3V";
  
  cv_range_t current = cv_get_range();
  uint32_t current_idx = 0;
  switch (current) {
    case CV_RANGE_10V: current_idx = 0; break;
    case CV_RANGE_BIPOLAR_10V: current_idx = 1; break;
    case CV_RANGE_5V: current_idx = 2; break;
    case CV_RANGE_BIPOLAR_5V: current_idx = 3; break;
    case CV_RANGE_3V3: current_idx = 4; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Voltage Range", options, current_idx, range_confirm_cb, NULL);
}

static void nav_to_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Voltage Range", range_roller_create);
}

// ============================================================================
// Pitch Standard Roller
// ============================================================================

static void pitch_std_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  cv_pitch_standard_t standards[] = {
    CV_PITCH_1V_OCTAVE_C0,
    CV_PITCH_1V_OCTAVE_C2,
    CV_PITCH_HZ_V
  };
  
  if (selected_index < 3) {
    cv_set_pitch_standard(standards[selected_index]);
    ESP_LOGI(TAG, "Pitch standard set to: %s", pitch_std_to_string(standards[selected_index]));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_create);
}

static lv_obj_t* pitch_std_roller_create(void) {
  static const char* options = "1V/Oct (C0@0V)\n1V/Oct (C2@0V)\nHz/V (Buchla)";
  
  cv_pitch_standard_t current = cv_get_pitch_standard();
  uint32_t current_idx = 0;
  switch (current) {
    case CV_PITCH_1V_OCTAVE_C0: current_idx = 0; break;
    case CV_PITCH_1V_OCTAVE_C2: current_idx = 1; break;
    case CV_PITCH_HZ_V: current_idx = 2; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Pitch Standard", options, current_idx, pitch_std_confirm_cb, NULL);
}

static void nav_to_pitch_std(void* user_data) {
  (void)user_data;
  menu_navigate_to("Pitch Standard", pitch_std_roller_create);
}

// ============================================================================
// Deadzone Roller
// ============================================================================

static void deadzone_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  cv_set_deadzone((uint8_t)selected_index);
  ESP_LOGI(TAG, "CV deadzone set to: %u", (unsigned)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_create);
}

static lv_obj_t* deadzone_roller_create(void) {
  // Deadzone values 0-10
  static const char* options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
  
  uint8_t current = cv_get_deadzone();
  if (current > 10) current = 10;
  
  return menu_create_roller_page("Deadzone", options, current, deadzone_confirm_cb, NULL);
}

static void nav_to_deadzone(void* user_data) {
  (void)user_data;
  menu_navigate_to("Deadzone", deadzone_roller_create);
}

// ============================================================================
// Calibration Wizard State
// ============================================================================

// Calibration timing
#define CAL_SAMPLE_INTERVAL_MS  50
#define CAL_DURATION_SECONDS    6      // Total calibration time

// Calibration state
static cv_range_t s_cal_range;
static int16_t s_cal_min = INT16_MAX;
static int16_t s_cal_max = INT16_MIN;
static uint32_t s_cal_samples_taken = 0;
static uint32_t s_cal_total_samples = 0;
static uint8_t s_cal_seconds_remaining = 0;
static bool s_cal_complete = false;
static lv_timer_t* s_cal_timer = NULL;
static lv_obj_t* s_cal_instruction_label = NULL;
static lv_obj_t* s_cal_countdown_label = NULL;
static lv_obj_t* s_cal_value_label = NULL;
static lv_obj_t* s_cal_screen = NULL;
static lv_obj_t* s_cal_done_btn = NULL;

static void get_sweep_instruction(cv_range_t range, char* buf, size_t buf_size) {
  const char* min_str;
  const char* max_str;
  
  switch (range) {
    case CV_RANGE_BIPOLAR_10V:
      min_str = "-10V";
      max_str = "+10V";
      break;
    case CV_RANGE_10V:
      min_str = "0V";
      max_str = "+10V";
      break;
    case CV_RANGE_BIPOLAR_5V:
      min_str = "-5V";
      max_str = "+5V";
      break;
    case CV_RANGE_5V:
      min_str = "0V";
      max_str = "+5V";
      break;
    case CV_RANGE_3V3:
      min_str = "0V";
      max_str = "3.3V";
      break;
    default:
      min_str = "MIN";
      max_str = "MAX";
      break;
  }
  
  snprintf(buf, buf_size, "Sweep voltage\nbetween %s and %s", min_str, max_str);
}

static void calibration_cleanup(void) {
  if (s_cal_timer) {
    lv_timer_delete(s_cal_timer);
    s_cal_timer = NULL;
  }
  s_cal_instruction_label = NULL;
  s_cal_countdown_label = NULL;
  s_cal_value_label = NULL;
  s_cal_screen = NULL;
  s_cal_done_btn = NULL;
}

static void finalize_calibration(void) {
  s_cal_complete = true;
  
  if (s_cal_timer) {
    lv_timer_delete(s_cal_timer);
    s_cal_timer = NULL;
  }
  
  // Ensure min < max (swap if wired backwards)
  if (s_cal_min > s_cal_max) {
    int16_t temp = s_cal_min;
    s_cal_min = s_cal_max;
    s_cal_max = temp;
  }
  
  int16_t swing = s_cal_max - s_cal_min;
  int16_t margin = swing / 100;  // 1% margin
  int16_t final_min = s_cal_min + margin;
  int16_t final_max = s_cal_max - margin;
  
  if (final_min < final_max && swing > 100) {
    cv_set_calibration(s_cal_range, final_min, final_max);
    ESP_LOGI(TAG, "Calibration saved for %s: min=%d, max=%d",
      range_to_string(s_cal_range), final_min, final_max);
    
    if (s_cal_instruction_label) {
      lv_label_set_text(s_cal_instruction_label,
        "Calibration\nComplete!");
    }
    if (s_cal_countdown_label) {
      lv_label_set_text(s_cal_countdown_label, "Press any button\nto continue");
    }
    if (s_cal_value_label) {
      static char result[64];
      snprintf(result, sizeof(result), "Range: %d - %d", final_min, final_max);
      lv_label_set_text(s_cal_value_label, result);
    }
  } else {
    ESP_LOGW(TAG, "Calibration failed: insufficient range (swing=%d)", swing);
    if (s_cal_instruction_label) {
      lv_label_set_text(s_cal_instruction_label, "Calibration\nFailed!");
    }
    if (s_cal_countdown_label) {
      lv_label_set_text(s_cal_countdown_label, "Insufficient voltage\nrange detected");
    }
    if (s_cal_value_label) {
      lv_label_set_text(s_cal_value_label, "Press any button");
    }
  }
}

static void calibration_timer_cb(lv_timer_t* timer) {
  (void)timer;
  
  if (s_cal_complete) return;
  
  // Read current value and track min/max
  int16_t current = cv_read_raw_now();
  if (current < s_cal_min) s_cal_min = current;
  if (current > s_cal_max) s_cal_max = current;
  
  s_cal_samples_taken++;
  
  // Update ADC display
  if (s_cal_value_label) {
    static char buf[48];
    snprintf(buf, sizeof(buf), "ADC: %d  (min:%d max:%d)", current, s_cal_min, s_cal_max);
    lv_label_set_text(s_cal_value_label, buf);
  }
  
  // Update countdown every second
  uint8_t new_seconds = (s_cal_total_samples - s_cal_samples_taken) * 
    CAL_SAMPLE_INTERVAL_MS / 1000;
  if (new_seconds != s_cal_seconds_remaining) {
    s_cal_seconds_remaining = new_seconds;
    if (s_cal_countdown_label) {
      static char countdown_buf[16];
      snprintf(countdown_buf, sizeof(countdown_buf), "%u...", s_cal_seconds_remaining + 1);
      lv_label_set_text(s_cal_countdown_label, countdown_buf);
    }
  }
  
  // Check if calibration time is complete
  if (s_cal_samples_taken >= s_cal_total_samples) {
    finalize_calibration();
  }
}

static bool calibration_handle_back(void) {
  ESP_LOGI(TAG, "Calibration cancelled/completed by user");
  calibration_cleanup();
  menu_set_custom_back_handler(NULL);
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_create);
  return true;
}

static void calibration_click_cb(lv_event_t* e) {
  (void)e;
  calibration_handle_back();
}

static lv_obj_t* calibration_page_create(void) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Initialize calibration state
  s_cal_range = cv_get_range();
  s_cal_min = INT16_MAX;
  s_cal_max = INT16_MIN;
  s_cal_samples_taken = 0;
  s_cal_total_samples = (CAL_DURATION_SECONDS * 1000) / CAL_SAMPLE_INTERVAL_MS;
  s_cal_seconds_remaining = CAL_DURATION_SECONDS;
  s_cal_complete = false;
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  s_cal_screen = screen;
  
  // Title bar
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
  
  // Title with range
  static char title[32];
  snprintf(title, sizeof(title), "Calibrate %s", range_to_short_string(s_cal_range));
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);
  
  // Content area
  lv_obj_t* content = lv_obj_create(screen);
  lv_obj_set_size(content, disp_w, disp_h - title_bar_h);
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 10, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  
  // Instruction label (sweep between min and max)
  static char instruction[64];
  get_sweep_instruction(s_cal_range, instruction, sizeof(instruction));
  s_cal_instruction_label = lv_label_create(content);
  lv_label_set_text(s_cal_instruction_label, instruction);
  lv_obj_set_style_text_color(s_cal_instruction_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cal_instruction_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(s_cal_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_cal_instruction_label, LV_ALIGN_TOP_MID, 0, 15);
  
  // Countdown label (large, centered)
  s_cal_countdown_label = lv_label_create(content);
  static char countdown_buf[16];
  snprintf(countdown_buf, sizeof(countdown_buf), "%u...", CAL_DURATION_SECONDS);
  lv_label_set_text(s_cal_countdown_label, countdown_buf);
  lv_obj_set_style_text_color(s_cal_countdown_label, lv_color_make(100, 200, 255), 0);
  lv_obj_set_style_text_font(s_cal_countdown_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(s_cal_countdown_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_cal_countdown_label, LV_ALIGN_CENTER, 0, 10);
  
  // Value label (shows ADC and min/max tracking)
  s_cal_value_label = lv_label_create(content);
  lv_label_set_text(s_cal_value_label, "ADC: ---");
  lv_obj_set_style_text_color(s_cal_value_label, lv_color_make(150, 150, 150), 0);
  lv_obj_set_style_text_font(s_cal_value_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_cal_value_label, LV_ALIGN_BOTTOM_MID, 0, -5);
  
  // Clickable area for completion/cancel
  s_cal_done_btn = lv_obj_create(content);
  lv_obj_set_size(s_cal_done_btn, disp_w - 20, disp_h - title_bar_h - 20);
  lv_obj_center(s_cal_done_btn);
  lv_obj_set_style_bg_opa(s_cal_done_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_cal_done_btn, 0, 0);
  lv_obj_add_flag(s_cal_done_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_cal_done_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_cal_done_btn, calibration_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Focus group
  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, s_cal_done_btn);
    lv_group_focus_obj(s_cal_done_btn);
  }
  
  // Custom back handler
  menu_set_custom_back_handler(calibration_handle_back);
  
  // Start timer
  s_cal_timer = lv_timer_create(calibration_timer_cb, CAL_SAMPLE_INTERVAL_MS, NULL);
  
  ESP_LOGI(TAG, "CV calibration wizard started for %s (%u seconds)",
    range_to_string(s_cal_range), CAL_DURATION_SECONDS);
  
  return screen;
}

static void nav_to_calibrate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Calibration", calibration_page_create);
}

// ============================================================================
// Main Settings CV Page
// ============================================================================

lv_obj_t* menu_page_cv_create(void) {
  ESP_LOGI(TAG, "Creating Settings CV page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Voltage Range
  cv_range_t range = cv_get_range();
  snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
    "Voltage Range\n%s", range_to_string(range));
  s_cv_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Calibrate (with current range in label, no second line)
  snprintf(s_calibrate_label[buf], sizeof(s_calibrate_label[buf]),
    "Calibrate %s\n", range_to_short_string(range));
  s_cv_items[item_count++] = (menu_item_t){
    s_calibrate_label[buf], nav_to_calibrate, NULL, true, MENU_ITEM_KIND_SUBMENU
  };
  
  // Pitch Standard
  cv_pitch_standard_t pitch_std = cv_get_pitch_standard();
  snprintf(s_pitch_std_label[buf], sizeof(s_pitch_std_label[buf]),
    "Pitch Standard\n%s", pitch_std_to_string(pitch_std));
  s_cv_items[item_count++] = (menu_item_t){s_pitch_std_label[buf], nav_to_pitch_std, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Deadzone
  uint8_t deadzone = cv_get_deadzone();
  snprintf(s_deadzone_label[buf], sizeof(s_deadzone_label[buf]),
    "Deadzone\n%u", (unsigned)deadzone);
  s_cv_items[item_count++] = (menu_item_t){s_deadzone_label[buf], nav_to_deadzone, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  return menu_create_page_2line("Control Voltage", s_cv_items, item_count);
}
