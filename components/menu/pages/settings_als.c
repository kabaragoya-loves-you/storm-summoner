#include "menu.h"
#include "sensor.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

#define TAG "MENU_SETTINGS_ALS"

// Forward declarations
lv_obj_t* menu_page_settings_als_create(void);
static lv_obj_t* calibration_page_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_ALS_ITEMS 5
static menu_item_t s_als_items[MAX_ALS_ITEMS];

static char s_deadzone_label[LABEL_BUFFER_SETS][32];
static char s_raw_mode_label[LABEL_BUFFER_SETS][40];
static char s_white_channel_label[LABEL_BUFFER_SETS][40];
static char s_calibrate_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// ============================================================================
// Calibration Wizard State
// ============================================================================

typedef enum {
  CAL_STEP_DARK = 0,
  CAL_STEP_BRIGHT,
  CAL_STEP_DARK_2,
  CAL_STEP_BRIGHT_2,
  CAL_STEP_COMPLETE
} calibration_step_t;

static const char* s_cal_step_instructions[] = {
  "Cover the sensor\n(DARK)\nand hold...",
  "Expose sensor to\nBRIGHT light\nand hold...",
  "Cover the sensor\n(DARK)\nagain...",
  "Expose sensor to\nBRIGHT light\nagain...",
  "Calibration\nComplete!\n\nPress any button\nto continue"
};

// Stability detection settings
#define CAL_SAMPLE_INTERVAL_MS  50
#define CAL_STABILITY_WINDOW    20
#define CAL_STABILITY_THRESHOLD 100  // ALS can have more noise
#define CAL_STABLE_DURATION     20

// Calibration state
static calibration_step_t s_cal_step = CAL_STEP_DARK;
static uint16_t s_cal_min = UINT16_MAX;
static uint16_t s_cal_max = 0;
static float s_cal_samples[CAL_STABILITY_WINDOW];
static uint8_t s_cal_sample_index = 0;
static uint8_t s_cal_sample_count = 0;
static uint8_t s_cal_stable_count = 0;
static lv_timer_t* s_cal_timer = NULL;
static lv_obj_t* s_cal_instruction_label = NULL;
static lv_obj_t* s_cal_value_label = NULL;
static lv_obj_t* s_cal_screen = NULL;
static lv_obj_t* s_cal_done_btn = NULL;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// ============================================================================
// Calibration Helper Functions
// ============================================================================

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

static bool is_value_stable(void) {
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) return false;
  
  // Calculate mean
  float sum = 0;
  for (int i = 0; i < CAL_STABILITY_WINDOW; i++) {
    sum += s_cal_samples[i];
  }
  float mean = sum / CAL_STABILITY_WINDOW;
  
  // Calculate variance
  float variance = 0;
  for (int i = 0; i < CAL_STABILITY_WINDOW; i++) {
    float diff = s_cal_samples[i] - mean;
    variance += diff * diff;
  }
  variance /= CAL_STABILITY_WINDOW;
  
  return variance < (CAL_STABILITY_THRESHOLD * CAL_STABILITY_THRESHOLD);
}

static void advance_calibration_step(void) {
  // Get stable value (average of samples)
  float sum = 0;
  for (int i = 0; i < CAL_STABILITY_WINDOW; i++) {
    sum += s_cal_samples[i];
  }
  uint16_t stable_value = (uint16_t)(sum / CAL_STABILITY_WINDOW);
  
  ESP_LOGI(TAG, "Calibration step %d complete, value=%u", s_cal_step, stable_value);
  
  // Update min/max based on step (DARK = low value, BRIGHT = high value)
  if (s_cal_step == CAL_STEP_DARK || s_cal_step == CAL_STEP_DARK_2) {
    if (stable_value < s_cal_min) {
      s_cal_min = stable_value;
    }
  } else if (s_cal_step == CAL_STEP_BRIGHT || s_cal_step == CAL_STEP_BRIGHT_2) {
    if (stable_value > s_cal_max) {
      s_cal_max = stable_value;
    }
  }
  
  // Advance to next step
  s_cal_step++;
  s_cal_stable_count = 0;
  s_cal_sample_count = 0;
  s_cal_sample_index = 0;
  
  // Update instruction label
  if (s_cal_instruction_label) {
    lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[s_cal_step]);
  }
  
  // If complete, finalize calibration
  if (s_cal_step == CAL_STEP_COMPLETE) {
    // Stop the timer
    if (s_cal_timer) {
      lv_timer_delete(s_cal_timer);
      s_cal_timer = NULL;
    }
    
    uint16_t swing = s_cal_max - s_cal_min;
    
    if (s_cal_min < s_cal_max && swing > 100) {
      als_set_calibration(s_cal_min, s_cal_max);
      ESP_LOGI(TAG, "Calibration saved: min=%u, max=%u (swing=%u)",
        s_cal_min, s_cal_max, swing);
      
      // Update value label to show result
      if (s_cal_value_label) {
        static char result[64];
        snprintf(result, sizeof(result), "Range: %u - %u", s_cal_min, s_cal_max);
        lv_label_set_text(s_cal_value_label, result);
      }
    } else {
      ESP_LOGW(TAG, "Calibration failed: insufficient range (swing=%u)", swing);
      if (s_cal_value_label) {
        lv_label_set_text(s_cal_value_label, "Insufficient range!");
      }
    }
  }
}

static void calibration_timer_cb(lv_timer_t* timer) {
  (void)timer;
  
  if (s_cal_step == CAL_STEP_COMPLETE) return;
  
  uint16_t current = get_als();
  
  // Add to ring buffer
  s_cal_samples[s_cal_sample_index] = (float)current;
  s_cal_sample_index = (s_cal_sample_index + 1) % CAL_STABILITY_WINDOW;
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) {
    s_cal_sample_count++;
  }
  
  // Update value display
  if (s_cal_value_label) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "Raw: %u", current);
    lv_label_set_text(s_cal_value_label, buf);
  }
  
  // Check stability
  if (is_value_stable()) {
    s_cal_stable_count++;
    if (s_cal_stable_count >= CAL_STABLE_DURATION) {
      advance_calibration_step();
    }
  } else {
    s_cal_stable_count = 0;
  }
}

static bool calibration_handle_back(void) {
  ESP_LOGI(TAG, "Calibration cancelled/completed by user");
  calibration_cleanup();
  menu_set_custom_back_handler(NULL);
  menu_navigate_back_then_to(2, "Ambient Light", menu_page_settings_als_create);
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
  s_cal_step = CAL_STEP_DARK;
  s_cal_min = UINT16_MAX;
  s_cal_max = 0;
  s_cal_sample_count = 0;
  s_cal_sample_index = 0;
  s_cal_stable_count = 0;
  
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
  
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, "Calibration");
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);
  
  // Main content area
  lv_obj_t* content = lv_obj_create(screen);
  lv_obj_set_size(content, disp_w, disp_h - title_bar_h);
  lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(content, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 10, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  
  // Instruction label
  s_cal_instruction_label = lv_label_create(content);
  lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[CAL_STEP_DARK]);
  lv_obj_set_style_text_color(s_cal_instruction_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cal_instruction_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(s_cal_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_cal_instruction_label, LV_ALIGN_CENTER, 0, -15);
  
  // Value display label
  s_cal_value_label = lv_label_create(content);
  lv_label_set_text(s_cal_value_label, "Raw: ---");
  lv_obj_set_style_text_color(s_cal_value_label, lv_color_make(150, 150, 150), 0);
  lv_obj_set_style_text_font(s_cal_value_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_cal_value_label, LV_ALIGN_BOTTOM_MID, 0, -5);
  
  // Hidden clickable area
  s_cal_done_btn = lv_obj_create(content);
  lv_obj_set_size(s_cal_done_btn, disp_w - 20, disp_h - title_bar_h - 20);
  lv_obj_center(s_cal_done_btn);
  lv_obj_set_style_bg_opa(s_cal_done_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_cal_done_btn, 0, 0);
  lv_obj_add_flag(s_cal_done_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_cal_done_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_cal_done_btn, calibration_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Add to focus group
  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, s_cal_done_btn);
    lv_group_focus_obj(s_cal_done_btn);
  }
  
  // Set custom back handler
  menu_set_custom_back_handler(calibration_handle_back);
  
  // Start the calibration timer
  s_cal_timer = lv_timer_create(calibration_timer_cb, CAL_SAMPLE_INTERVAL_MS, NULL);
  
  ESP_LOGI(TAG, "ALS calibration wizard started");
  
  return screen;
}

static void nav_to_calibrate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Calibration", calibration_page_create);
}

// ============================================================================
// Deadzone Roller
// ============================================================================

static void deadzone_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  als_set_deadzone((uint8_t)selected_index);
  ESP_LOGI(TAG, "ALS deadzone set to: %u", (unsigned)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Ambient Light", menu_page_settings_als_create);
}

static lv_obj_t* deadzone_roller_create(void) {
  static const char* options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
  
  uint8_t current = als_get_deadzone();
  if (current > 10) current = 10;
  
  return menu_create_roller_page("Deadzone", options, current, deadzone_confirm_cb, NULL);
}

static void nav_to_deadzone(void* user_data) {
  (void)user_data;
  menu_navigate_to("Deadzone", deadzone_roller_create);
}

// ============================================================================
// Raw Mode Roller
// ============================================================================

static void raw_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  als_set_raw_mode(selected_index == 1);
  ESP_LOGI(TAG, "ALS raw mode %s", selected_index == 1 ? "enabled" : "disabled");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Ambient Light", menu_page_settings_als_create);
}

static lv_obj_t* raw_mode_roller_create(void) {
  static const char* options = "Filtered\nRaw";
  
  uint32_t current = als_get_raw_mode() ? 1 : 0;
  
  return menu_create_roller_page("Mode", options, current, raw_mode_confirm_cb, NULL);
}

static void nav_to_raw_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", raw_mode_roller_create);
}

// ============================================================================
// White Channel Roller
// ============================================================================

static void white_channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  als_set_use_white_channel(selected_index == 1);
  ESP_LOGI(TAG, "ALS white channel %s", selected_index == 1 ? "enabled" : "disabled");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Ambient Light", menu_page_settings_als_create);
}

static lv_obj_t* white_channel_roller_create(void) {
  static const char* options = "ALS Channel\nWhite Channel";
  
  uint32_t current = als_get_use_white_channel() ? 1 : 0;
  
  return menu_create_roller_page("Source", options, current, white_channel_confirm_cb, NULL);
}

static void nav_to_white_channel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Source", white_channel_roller_create);
}

// ============================================================================
// Main Settings ALS Page
// ============================================================================

lv_obj_t* menu_page_settings_als_create(void) {
  ESP_LOGI(TAG, "Creating Settings Ambient Light page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Calibrate (at top, no second line)
  snprintf(s_calibrate_label[buf], sizeof(s_calibrate_label[buf]), "Calibrate\n");
  s_als_items[item_count++] = (menu_item_t){s_calibrate_label[buf], nav_to_calibrate, NULL, true};
  
  // Filter Mode
  bool raw_mode = als_get_raw_mode();
  snprintf(s_raw_mode_label[buf], sizeof(s_raw_mode_label[buf]),
    "Filter Mode\n%s", raw_mode ? "Raw" : "Filtered");
  s_als_items[item_count++] = (menu_item_t){s_raw_mode_label[buf], nav_to_raw_mode, NULL, true};
  
  // Source
  bool white_ch = als_get_use_white_channel();
  snprintf(s_white_channel_label[buf], sizeof(s_white_channel_label[buf]),
    "Source\n%s", white_ch ? "White Channel" : "ALS Channel");
  s_als_items[item_count++] = (menu_item_t){s_white_channel_label[buf], nav_to_white_channel, NULL, true};
  
  // Deadzone (at bottom)
  uint8_t deadzone = als_get_deadzone();
  snprintf(s_deadzone_label[buf], sizeof(s_deadzone_label[buf]),
    "Deadzone\n%u", (unsigned)deadzone);
  s_als_items[item_count++] = (menu_item_t){s_deadzone_label[buf], nav_to_deadzone, NULL, true};
  
  return menu_create_page_2line("Ambient Light", s_als_items, item_count);
}

