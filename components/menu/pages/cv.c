#include "menu.h"
#include "menu_pages.h"
#include "cv.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_SETTINGS_CV"

// Forward declarations
lv_obj_t* menu_page_cv_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_CV_ITEMS 5
static menu_item_t s_cv_items[MAX_CV_ITEMS];

static char s_range_label[LABEL_BUFFER_SETS][40];
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
// Calibrate (TODO: wizard like expression)
// ============================================================================

static void nav_to_calibrate(void* user_data) {
  (void)user_data;
  // TODO: Implement calibration wizard similar to expression
  ESP_LOGI(TAG, "Calibrate - TODO: implement wizard");
  
  // For now, show info text
  menu_navigate_to_info("Calibrate", 
    "CV Calibration\n\n"
    "Use console command:\n"
    "cv calibrate <range> <ms>\n\n"
    "Example:\n"
    "cv calibrate 5v 5000");
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
  s_cv_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true};
  
  // Pitch Standard
  cv_pitch_standard_t pitch_std = cv_get_pitch_standard();
  snprintf(s_pitch_std_label[buf], sizeof(s_pitch_std_label[buf]),
    "Pitch Standard\n%s", pitch_std_to_string(pitch_std));
  s_cv_items[item_count++] = (menu_item_t){s_pitch_std_label[buf], nav_to_pitch_std, NULL, true};
  
  // Deadzone
  uint8_t deadzone = cv_get_deadzone();
  snprintf(s_deadzone_label[buf], sizeof(s_deadzone_label[buf]),
    "Deadzone\n%u", (unsigned)deadzone);
  s_cv_items[item_count++] = (menu_item_t){s_deadzone_label[buf], nav_to_deadzone, NULL, true};
  
  // Calibrate
  s_cv_items[item_count++] = (menu_item_t){"Calibrate\n(Console)", nav_to_calibrate, NULL, true};
  
  return menu_create_page_2line("Control Voltage", s_cv_items, item_count);
}
