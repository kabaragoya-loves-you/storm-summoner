#include "menu.h"
#include "menu_pages.h"
#include "cv.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_CV"

static void show_info(void* user_data) {
  (void)user_data;
  bool connected = cv_is_cable_connected();
  cv_range_t range = cv_get_range();
  cv_pitch_standard_t pitch_std = cv_get_pitch_standard();
  uint8_t deadzone = cv_get_deadzone();
  
  const char* range_str;
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case CV_RANGE_10V: range_str = "0-10V"; break;
    case CV_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case CV_RANGE_5V: range_str = "0-5V"; break;
    case CV_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  const char* pitch_std_str;
  switch (pitch_std) {
    case CV_PITCH_1V_OCTAVE_C0: pitch_std_str = "1V/Oct (C0@0V)"; break;
    case CV_PITCH_1V_OCTAVE_C2: pitch_std_str = "1V/Oct (C2@0V)"; break;
    case CV_PITCH_HZ_V: pitch_std_str = "Hz/V (Buchla)"; break;
    default: pitch_std_str = "Unknown"; break;
  }
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "CV INPUT\n"
    "Voltage range: %s\n"
    "Pitch standard: %s\n"
    "Deadzone: %u\n"
    "Cable: %s",
    range_str, pitch_std_str, (unsigned)deadzone, connected ? "connected" : "disconnected");
  
  menu_navigate_to_info("CV Info", info_text);
}

static void set_range_10v(void* user_data) { (void)user_data; cv_set_range(CV_RANGE_10V); ESP_LOGI(TAG, "Range: 0-10V"); }
static void set_range_bi10v(void* user_data) { (void)user_data; cv_set_range(CV_RANGE_BIPOLAR_10V); ESP_LOGI(TAG, "Range: ±10V"); }
static void set_range_5v(void* user_data) { (void)user_data; cv_set_range(CV_RANGE_5V); ESP_LOGI(TAG, "Range: 0-5V"); }
static void set_range_bi5v(void* user_data) { (void)user_data; cv_set_range(CV_RANGE_BIPOLAR_5V); ESP_LOGI(TAG, "Range: ±5V"); }
static void set_range_3v3(void* user_data) { (void)user_data; cv_set_range(CV_RANGE_3V3); ESP_LOGI(TAG, "Range: 0-3.3V"); }

static void action_calibrate(void* user_data) {
  (void)user_data;
  // TODO: Implement calibration UI
  ESP_LOGI(TAG, "Calibrate - TODO: implement");
}

lv_obj_t* menu_page_cv_create(void) {
  ESP_LOGI(TAG, "Creating CV page");
  
  static menu_item_t cv_items[] = {
    { "Info", show_info, NULL, false },
    { "Range: 0-10V", set_range_10v, NULL, false },
    { "Range: ±10V", set_range_bi10v, NULL, false },
    { "Range: 0-5V", set_range_5v, NULL, false },
    { "Range: ±5V", set_range_bi5v, NULL, false },
    { "Range: 0-3.3V", set_range_3v3, NULL, false },
    { "Calibrate", action_calibrate, NULL, false }
  };
  
  return menu_create_page("CV", cv_items, 
    sizeof(cv_items) / sizeof(cv_items[0]));
}
