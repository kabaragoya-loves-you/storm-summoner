#include "menu.h"
#include "sensor.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

#define TAG "MENU_SETTINGS_PROXIMITY"

// Forward declarations
lv_obj_t* menu_page_settings_proximity_create(void);
static lv_obj_t* calibration_page_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_PROX_ITEMS 10
static menu_item_t s_prox_items[MAX_PROX_ITEMS];

static char s_deadzone_label[LABEL_BUFFER_SETS][32];
static char s_hysteresis_label[LABEL_BUFFER_SETS][40];
static char s_rest_pos_label[LABEL_BUFFER_SETS][40];
static char s_return_speed_label[LABEL_BUFFER_SETS][40];
static char s_timeout_label[LABEL_BUFFER_SETS][40];
static char s_note_silence_label[LABEL_BUFFER_SETS][40];
static char s_calibrate_label[LABEL_BUFFER_SETS][32];
static char s_sunlight_label[LABEL_BUFFER_SETS][48];
static char s_gamma_label[LABEL_BUFFER_SETS][40];

static bool s_callback_in_progress = false;

// ============================================================================
// Calibration Wizard State
// ============================================================================

typedef enum {
  CAL_STEP_FAR = 0,
  CAL_STEP_NEAR,
  CAL_STEP_FAR_2,
  CAL_STEP_NEAR_2,
  CAL_STEP_COMPLETE
} calibration_step_t;

static const char* s_cal_step_instructions[] = {
  "Move hand\nAWAY from sensor\nand hold...",
  "Move hand\nCLOSE to sensor\nand hold...",
  "Move hand\nAWAY from sensor\nagain...",
  "Move hand\nCLOSE to sensor\nagain...",
  "Calibration\nComplete!\n\nPress any button\nto continue"
};

// Stability detection settings
#define CAL_SAMPLE_INTERVAL_MS  50
#define CAL_STABILITY_WINDOW    20
#define CAL_STABILITY_THRESHOLD 50   // Proximity has more noise
#define CAL_STABLE_DURATION     20

// Calibration state
static calibration_step_t s_cal_step = CAL_STEP_FAR;
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

static const char* return_speed_to_string(proximity_return_speed_t speed) {
  switch (speed) {
    case PROXIMITY_RETURN_INSTANT: return "Instant";
    case PROXIMITY_RETURN_FAST: return "Fast";
    case PROXIMITY_RETURN_MEDIUM: return "Medium";
    case PROXIMITY_RETURN_SLOW: return "Slow";
    default: return "Unknown";
  }
}

static const char* timeout_to_string(proximity_timeout_t timeout) {
  switch (timeout) {
    case PROXIMITY_TIMEOUT_FAST: return "Fast (500ms)";
    case PROXIMITY_TIMEOUT_MEDIUM: return "Medium (1s)";
    case PROXIMITY_TIMEOUT_SLOW: return "Slow (5s)";
    default: return "Unknown";
  }
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
  
  // Update min/max based on step (FAR = low value, NEAR = high value)
  if (s_cal_step == CAL_STEP_FAR || s_cal_step == CAL_STEP_FAR_2) {
    if (stable_value < s_cal_min) {
      s_cal_min = stable_value;
    }
  } else if (s_cal_step == CAL_STEP_NEAR || s_cal_step == CAL_STEP_NEAR_2) {
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
    
    if (s_cal_min < s_cal_max && swing > 50) {
      proximity_set_calibration(s_cal_min, s_cal_max);
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
  
  uint16_t current = get_ps();
  
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
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
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
  s_cal_step = CAL_STEP_FAR;
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
  lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[CAL_STEP_FAR]);
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
  
  ESP_LOGI(TAG, "Proximity calibration wizard started");
  
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
  
  proximity_set_deadzone((uint8_t)selected_index);
  ESP_LOGI(TAG, "Proximity deadzone set to: %u", (unsigned)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* deadzone_roller_create(void) {
  static const char* options = "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10";
  
  uint8_t current = proximity_get_deadzone();
  if (current > 10) current = 10;
  
  return menu_create_roller_page("Deadzone", options, current, deadzone_confirm_cb, NULL);
}

static void nav_to_deadzone(void* user_data) {
  (void)user_data;
  menu_navigate_to("Deadzone", deadzone_roller_create);
}

// ============================================================================
// Hysteresis Enable Roller
// ============================================================================

static void hysteresis_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  proximity_set_hysteresis_enabled(selected_index == 1);
  ESP_LOGI(TAG, "Hysteresis %s", selected_index == 1 ? "enabled" : "disabled");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* hysteresis_roller_create(void) {
  static const char* options = "Disabled\nEnabled";
  
  uint32_t current = proximity_get_hysteresis_enabled() ? 1 : 0;
  
  return menu_create_roller_page("Hysteresis", options, current, hysteresis_confirm_cb, NULL);
}

static void nav_to_hysteresis(void* user_data) {
  (void)user_data;
  menu_navigate_to("Hysteresis", hysteresis_roller_create);
}

// ============================================================================
// Rest Position Roller
// ============================================================================

static void rest_pos_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Map 0-127 to actual value (every 8 steps for 16 options)
  uint8_t value = (uint8_t)(selected_index * 8);
  if (selected_index == 15) value = 127;  // Last option is 127
  
  proximity_set_rest_position(value);
  ESP_LOGI(TAG, "Rest position set to: %u", value);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* rest_pos_roller_create(void) {
  // 16 options: 0, 8, 16, ..., 120, 127
  static const char* options = 
    "0\n8\n16\n24\n32\n40\n48\n56\n64\n72\n80\n88\n96\n104\n112\n127";
  
  uint8_t current = proximity_get_rest_position();
  uint32_t idx = current / 8;
  if (idx > 15) idx = 15;
  
  return menu_create_roller_page("Rest Position", options, idx, rest_pos_confirm_cb, NULL);
}

static void nav_to_rest_pos(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rest Position", rest_pos_roller_create);
}

// ============================================================================
// Return Speed Roller
// ============================================================================

static void return_speed_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  proximity_return_speed_t speeds[] = {
    PROXIMITY_RETURN_INSTANT,
    PROXIMITY_RETURN_FAST,
    PROXIMITY_RETURN_MEDIUM,
    PROXIMITY_RETURN_SLOW
  };
  
  if (selected_index < 4) {
    proximity_set_return_speed(speeds[selected_index]);
    ESP_LOGI(TAG, "Return speed set to: %s", return_speed_to_string(speeds[selected_index]));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* return_speed_roller_create(void) {
  static const char* options = "Instant\nFast\nMedium\nSlow";
  
  uint32_t current = 0;
  switch (proximity_get_return_speed()) {
    case PROXIMITY_RETURN_INSTANT: current = 0; break;
    case PROXIMITY_RETURN_FAST: current = 1; break;
    case PROXIMITY_RETURN_MEDIUM: current = 2; break;
    case PROXIMITY_RETURN_SLOW: current = 3; break;
  }
  
  return menu_create_roller_page("Return Speed", options, current, return_speed_confirm_cb, NULL);
}

static void nav_to_return_speed(void* user_data) {
  (void)user_data;
  menu_navigate_to("Return Speed", return_speed_roller_create);
}

// ============================================================================
// Timeout Roller
// ============================================================================

static void timeout_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  proximity_timeout_t timeouts[] = {
    PROXIMITY_TIMEOUT_FAST,
    PROXIMITY_TIMEOUT_MEDIUM,
    PROXIMITY_TIMEOUT_SLOW
  };
  
  if (selected_index < 3) {
    proximity_set_timeout(timeouts[selected_index]);
    ESP_LOGI(TAG, "Timeout set to: %s", timeout_to_string(timeouts[selected_index]));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* timeout_roller_create(void) {
  static const char* options = "Fast (500ms)\nMedium (1s)\nSlow (5s)";
  
  uint32_t current = 0;
  switch (proximity_get_timeout()) {
    case PROXIMITY_TIMEOUT_FAST: current = 0; break;
    case PROXIMITY_TIMEOUT_MEDIUM: current = 1; break;
    case PROXIMITY_TIMEOUT_SLOW: current = 2; break;
  }
  
  return menu_create_roller_page("Timeout", options, current, timeout_confirm_cb, NULL);
}

static void nav_to_timeout(void* user_data) {
  (void)user_data;
  menu_navigate_to("Timeout", timeout_roller_create);
}

// ============================================================================
// Note Silence Roller (for note mode out-of-range behavior)
// ============================================================================

static void note_silence_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  proximity_set_note_silence_on_low(selected_index == 1);
  ESP_LOGI(TAG, "Note silence on low %s", selected_index == 1 ? "enabled" : "disabled");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* note_silence_roller_create(void) {
  static const char* options = "Disabled\nEnabled";
  
  uint32_t current = proximity_get_note_silence_on_low() ? 1 : 0;
  
  return menu_create_roller_page("Note Silence", options, current, note_silence_confirm_cb, NULL);
}

static void nav_to_note_silence(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note Silence", note_silence_roller_create);
}

// ============================================================================
// Sunlight Cancellation Roller (ambient IR rejection)
// ============================================================================

static void sunlight_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  proximity_set_sunlight_cancel(selected_index == 1);
  ESP_LOGI(TAG, "Sunlight cancellation %s", selected_index == 1 ? "enabled" : "disabled");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* sunlight_roller_create(void) {
  static const char* options = "Off\nOn";
  
  uint32_t current = proximity_get_sunlight_cancel() ? 1 : 0;
  
  return menu_create_roller_page("IR Rejection", options, current, sunlight_confirm_cb, NULL);
}

static void nav_to_sunlight(void* user_data) {
  (void)user_data;
  menu_navigate_to("IR Rejection", sunlight_roller_create);
}

// ============================================================================
// Gamma Roller (inverse-square compensation)
// ============================================================================

static void gamma_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Map index 0-20 to gamma 0-100 (steps of 5)
  uint8_t gamma = (uint8_t)(selected_index * 5);
  proximity_set_gamma(gamma);
  ESP_LOGI(TAG, "Gamma set to: %u (%.2f)", gamma, 0.15f + gamma * 0.0085f);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_settings_proximity_create);
}

static lv_obj_t* gamma_roller_create(void) {
  // 21 options: 0, 5, 10, ..., 100 (maps to gamma 0.15-1.00)
  static const char* options = 
    "0 (0.15)\n5 (0.19)\n10 (0.24)\n15 (0.28)\n20 (0.32)\n"
    "25 (0.36)\n30 (0.41)\n35 (0.45)\n40 (0.49)\n45 (0.53)\n"
    "50 (0.58)\n55 (0.62)\n60 (0.66)\n65 (0.70)\n70 (0.75)\n"
    "75 (0.79)\n80 (0.83)\n85 (0.87)\n90 (0.92)\n95 (0.96)\n100 (1.00)";
  
  uint8_t current = proximity_get_gamma();
  uint32_t idx = current / 5;
  if (idx > 20) idx = 20;
  
  return menu_create_roller_page("Gamma", options, idx, gamma_confirm_cb, NULL);
}

static void nav_to_gamma(void* user_data) {
  (void)user_data;
  menu_navigate_to("Gamma", gamma_roller_create);
}

// ============================================================================
// Main Settings Proximity Page
// ============================================================================

lv_obj_t* menu_page_settings_proximity_create(void) {
  ESP_LOGI(TAG, "Creating Settings Proximity page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Calibrate (at top, no second line)
  snprintf(s_calibrate_label[buf], sizeof(s_calibrate_label[buf]), "Calibrate\n");
  s_prox_items[item_count++] = (menu_item_t){
    s_calibrate_label[buf], nav_to_calibrate, NULL, true, MENU_ITEM_KIND_SUBMENU
  };
  
  // Sunlight/IR Rejection (ambient IR cancellation)
  bool sunlight_enabled = proximity_get_sunlight_cancel();
  snprintf(s_sunlight_label[buf], sizeof(s_sunlight_label[buf]),
    "IR Rejection\n%s", sunlight_enabled ? "On" : "Off");
  s_prox_items[item_count++] = (menu_item_t){s_sunlight_label[buf], nav_to_sunlight, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Gamma (inverse-square compensation)
  uint8_t gamma = proximity_get_gamma();
  snprintf(s_gamma_label[buf], sizeof(s_gamma_label[buf]),
    "Gamma\n%u (%.2f)", gamma, 0.15f + gamma * 0.0085f);
  s_prox_items[item_count++] = (menu_item_t){s_gamma_label[buf], nav_to_gamma, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Hysteresis Enable (for CC mode)
  bool hyst_enabled = proximity_get_hysteresis_enabled();
  snprintf(s_hysteresis_label[buf], sizeof(s_hysteresis_label[buf]),
    "Hysteresis\n%s", hyst_enabled ? "Enabled" : "Disabled");
  s_prox_items[item_count++] = (menu_item_t){s_hysteresis_label[buf], nav_to_hysteresis, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Only show hysteresis-related settings if enabled
  if (hyst_enabled) {
    // Rest Position
    uint8_t rest_pos = proximity_get_rest_position();
    snprintf(s_rest_pos_label[buf], sizeof(s_rest_pos_label[buf]),
      "Rest Position\n%u", (unsigned)rest_pos);
    s_prox_items[item_count++] = (menu_item_t){s_rest_pos_label[buf], nav_to_rest_pos, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    // Return Speed
    proximity_return_speed_t speed = proximity_get_return_speed();
    snprintf(s_return_speed_label[buf], sizeof(s_return_speed_label[buf]),
      "Return Speed\n%s", return_speed_to_string(speed));
    s_prox_items[item_count++] = (menu_item_t){s_return_speed_label[buf], nav_to_return_speed, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  // Note Silence on Low (for note mode)
  bool note_silence = proximity_get_note_silence_on_low();
  snprintf(s_note_silence_label[buf], sizeof(s_note_silence_label[buf]),
    "Note Silence\n%s", note_silence ? "Enabled" : "Disabled");
  s_prox_items[item_count++] = (menu_item_t){s_note_silence_label[buf], nav_to_note_silence, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Timeout - applies to both CC hysteresis and Note silence
  // Show when either feature is enabled
  if (hyst_enabled || note_silence) {
    proximity_timeout_t timeout = proximity_get_timeout();
    snprintf(s_timeout_label[buf], sizeof(s_timeout_label[buf]),
      "Timeout\n%s", timeout_to_string(timeout));
    s_prox_items[item_count++] = (menu_item_t){s_timeout_label[buf], nav_to_timeout, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  // Deadzone (at bottom)
  uint8_t deadzone = proximity_get_deadzone();
  snprintf(s_deadzone_label[buf], sizeof(s_deadzone_label[buf]),
    "Deadzone\n%u", (unsigned)deadzone);
  s_prox_items[item_count++] = (menu_item_t){s_deadzone_label[buf], nav_to_deadzone, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  return menu_create_page_2line("Proximity", s_prox_items, item_count);
}

