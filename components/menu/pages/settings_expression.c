#include "menu.h"
#include "expression.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

#define TAG "MENU_SETTINGS_EXPRESSION"

// Forward declarations
lv_obj_t* menu_page_settings_expression_create(void);
static lv_obj_t* calibration_page_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_EXPR_ITEMS 6
static menu_item_t s_expr_items[MAX_EXPR_ITEMS];

static char s_polarity_label[LABEL_BUFFER_SETS][40];
static char s_slow_delay_label[LABEL_BUFFER_SETS][32];
static char s_switch_type_label[LABEL_BUFFER_SETS][40];
static char s_calibrate_label[LABEL_BUFFER_SETS][32];
static char s_menu_nav_label[LABEL_BUFFER_SETS][40];

static bool s_callback_in_progress = false;

// ============================================================================
// Calibration Wizard State
// ============================================================================

typedef enum {
  CAL_STEP_HEEL_1 = 0,
  CAL_STEP_TOE_1,
  CAL_STEP_HEEL_2,
  CAL_STEP_TOE_2,
  CAL_STEP_COMPLETE
} calibration_step_t;

static const char* s_cal_step_instructions[] = {
  "Move pedal to\nHEEL position\nand hold...",
  "Move pedal to\nTOE position\nand hold...",
  "Move pedal to\nHEEL position\nagain...",
  "Move pedal to\nTOE position\nagain...",
  "Calibration\nComplete!\n\nPress any button\nto continue"
};

// Stability detection settings
#define CAL_SAMPLE_INTERVAL_MS  50
#define CAL_STABILITY_WINDOW    20     // Number of samples to check (1 second at 50ms)
#define CAL_STABILITY_THRESHOLD 30     // Max variance in ADC units to consider "stable"
#define CAL_STABLE_DURATION     20     // Samples to stay stable before advancing (~1 sec)

// Calibration state
static calibration_step_t s_cal_step = CAL_STEP_HEEL_1;
static int16_t s_cal_min = INT16_MAX;
static int16_t s_cal_max = INT16_MIN;
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
// TRS Polarity Roller
// ============================================================================

static void polarity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  expression_polarity_t polarity = (selected_index == 0) ? 
    EXPRESSION_POLARITY_TIP_ADC : EXPRESSION_POLARITY_RING_ADC;
  expression_set_polarity(polarity);
  
  ESP_LOGI(TAG, "TRS polarity set to: %s", 
    polarity == EXPRESSION_POLARITY_TIP_ADC ? "Tip->ADC" : "Ring->ADC");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_settings_expression_create);
}

static lv_obj_t* polarity_roller_create(void) {
  expression_polarity_t current = expression_get_polarity();
  uint32_t current_idx = (current == EXPRESSION_POLARITY_TIP_ADC) ? 0 : 1;
  
  return menu_create_roller_page("TRS Polarity", 
    "Tip\nRing", current_idx, polarity_confirm_cb, NULL);
}

static void nav_to_polarity(void* user_data) {
  (void)user_data;
  menu_navigate_to("TRS Polarity", polarity_roller_create);
}

// ============================================================================
// Slow Delay Roller
// ============================================================================

static void slow_delay_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Options: 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200
  static const uint8_t delays[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200};
  uint8_t num_delays = sizeof(delays) / sizeof(delays[0]);
  
  if (selected_index >= num_delays) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  expression_set_slow_delay(delays[selected_index]);
  ESP_LOGI(TAG, "Slow delay set to: %u ms", (unsigned)delays[selected_index]);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_settings_expression_create);
}

static lv_obj_t* slow_delay_roller_create(void) {
  static const char* options = "10\n20\n30\n40\n50\n60\n70\n80\n90\n100\n150\n200";
  static const uint8_t delays[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200};
  uint8_t num_delays = sizeof(delays) / sizeof(delays[0]);
  
  uint8_t current = expression_get_slow_delay();
  uint32_t current_idx = 4;  // Default to 50ms
  
  for (uint8_t i = 0; i < num_delays; i++) {
    if (delays[i] == current) {
      current_idx = i;
      break;
    }
  }
  
  return menu_create_roller_page("Slow Delay", options, current_idx, slow_delay_confirm_cb, NULL);
}

static void nav_to_slow_delay(void* user_data) {
  (void)user_data;
  menu_navigate_to("Slow Delay", slow_delay_roller_create);
}

// ============================================================================
// Menu Navigation Mode Roller
// ============================================================================

static void menu_nav_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  expression_menu_nav_mode_t mode = (expression_menu_nav_mode_t)selected_index;
  expression_set_menu_nav_mode(mode);
  
  ESP_LOGI(TAG, "Menu navigation mode set to: %u", (unsigned)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_settings_expression_create);
}

static lv_obj_t* menu_nav_roller_create(void) {
  expression_menu_nav_mode_t current = expression_get_menu_nav_mode();
  uint32_t current_idx = (uint32_t)current;
  if (current_idx > 2) current_idx = 1;  // Default to Heel Min
  
  return menu_create_roller_page("Menu Navigation",
    "Off\nHeel Min\nToe Min", current_idx, menu_nav_confirm_cb, NULL);
}

static void nav_to_menu_nav(void* user_data) {
  (void)user_data;
  menu_navigate_to("Menu Navigation", menu_nav_roller_create);
}

// ============================================================================
// Switch Type Roller (NO/NC for sustain/sostenuto pedals)
// ============================================================================

static void switch_type_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  pedal_switch_type_t type = (selected_index == 0) ? PEDAL_SWITCH_NO : PEDAL_SWITCH_NC;
  expression_set_pedal_switch_type(type);
  
  ESP_LOGI(TAG, "Switch type set to: %s", 
    type == PEDAL_SWITCH_NO ? "Normally Open" : "Normally Closed");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_settings_expression_create);
}

static lv_obj_t* switch_type_roller_create(void) {
  pedal_switch_type_t current = expression_get_pedal_switch_type();
  uint32_t current_idx = (current == PEDAL_SWITCH_NO) ? 0 : 1;
  
  return menu_create_roller_page("Switch Type", 
    "Normally Open\nNormally Closed", current_idx, switch_type_confirm_cb, NULL);
}

static void nav_to_switch_type(void* user_data) {
  (void)user_data;
  menu_navigate_to("Switch Type", switch_type_roller_create);
}

// ============================================================================
// Calibration Wizard
// ============================================================================

static void calibration_cleanup(void) {
  if (s_cal_timer) {
    lv_timer_delete(s_cal_timer);
    s_cal_timer = NULL;
  }
  s_cal_instruction_label = NULL;
  s_cal_value_label = NULL;
  s_cal_screen = NULL;
}

static bool is_value_stable(void) {
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) {
    return false;
  }
  
  // Find min/max in the window
  float win_min = s_cal_samples[0];
  float win_max = s_cal_samples[0];
  for (int i = 1; i < CAL_STABILITY_WINDOW; i++) {
    if (s_cal_samples[i] < win_min) win_min = s_cal_samples[i];
    if (s_cal_samples[i] > win_max) win_max = s_cal_samples[i];
  }
  
  float variance = win_max - win_min;
  return variance < CAL_STABILITY_THRESHOLD;
}

static void advance_calibration_step(void) {
  // Record current value to min/max tracking
  float current = expression_get_value();
  int16_t current_int = (int16_t)current;
  
  // Update min/max based on which step we're completing
  if (s_cal_step == CAL_STEP_HEEL_1 || s_cal_step == CAL_STEP_HEEL_2) {
    // Heel position - expect lower values
    if (current_int < s_cal_min) {
      s_cal_min = current_int;
    }
  } else if (s_cal_step == CAL_STEP_TOE_1 || s_cal_step == CAL_STEP_TOE_2) {
    // Toe position - expect higher values
    if (current_int > s_cal_max) {
      s_cal_max = current_int;
    }
  }
  
  ESP_LOGI(TAG, "Step %d complete: current=%d, min=%d, max=%d",
    s_cal_step, current_int, s_cal_min, s_cal_max);
  
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
    
    // Ensure min < max (swap if pedal is wired backwards)
    if (s_cal_min > s_cal_max) {
      int16_t temp = s_cal_min;
      s_cal_min = s_cal_max;
      s_cal_max = temp;
    }
    
    // Apply 1% margin for headroom
    int16_t swing = s_cal_max - s_cal_min;
    int16_t margin = swing / 100;
    int16_t final_min = s_cal_min + margin;
    int16_t final_max = s_cal_max - margin;
    
    if (final_min < final_max && swing > 100) {
      expression_set_range(final_min, final_max);
      ESP_LOGI(TAG, "Calibration saved: min=%d, max=%d (swing=%d)",
        final_min, final_max, swing);
      
      // Update value label to show result
      if (s_cal_value_label) {
        static char result[64];
        snprintf(result, sizeof(result), "Range: %d - %d", final_min, final_max);
        lv_label_set_text(s_cal_value_label, result);
      }
    } else {
      ESP_LOGW(TAG, "Calibration failed: insufficient range (swing=%d)", swing);
      if (s_cal_value_label) {
        lv_label_set_text(s_cal_value_label, "Insufficient range!");
      }
    }
  }
}

static void calibration_timer_cb(lv_timer_t* timer) {
  (void)timer;
  
  // Don't process if already complete
  if (s_cal_step == CAL_STEP_COMPLETE) {
    return;
  }
  
  // Get current expression value
  float current = expression_get_value();
  
  // Add to ring buffer
  s_cal_samples[s_cal_sample_index] = current;
  s_cal_sample_index = (s_cal_sample_index + 1) % CAL_STABILITY_WINDOW;
  if (s_cal_sample_count < CAL_STABILITY_WINDOW) {
    s_cal_sample_count++;
  }
  
  // Update value display
  if (s_cal_value_label) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "ADC: %.0f", current);
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
  menu_navigate_back_then_to(2, "Expression", menu_page_settings_expression_create);
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
  s_cal_step = CAL_STEP_HEEL_1;
  s_cal_min = INT16_MAX;
  s_cal_max = INT16_MIN;
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
  
  // Title bar (matching menu style)
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
  
  // Instruction label (centered, large font)
  s_cal_instruction_label = lv_label_create(content);
  lv_label_set_text(s_cal_instruction_label, s_cal_step_instructions[CAL_STEP_HEEL_1]);
  lv_obj_set_style_text_color(s_cal_instruction_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(s_cal_instruction_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(s_cal_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_cal_instruction_label, LV_ALIGN_CENTER, 0, -15);
  
  // Value display label (bottom of screen, smaller)
  s_cal_value_label = lv_label_create(content);
  lv_label_set_text(s_cal_value_label, "ADC: ---");
  lv_obj_set_style_text_color(s_cal_value_label, lv_color_make(150, 150, 150), 0);
  lv_obj_set_style_text_font(s_cal_value_label, &lv_font_montserrat_12, 0);
  lv_obj_align(s_cal_value_label, LV_ALIGN_BOTTOM_MID, 0, -5);
  
  // Hidden clickable area for handling enter/Omega pad when complete
  s_cal_done_btn = lv_obj_create(content);
  lv_obj_set_size(s_cal_done_btn, disp_w - 20, disp_h - title_bar_h - 20);
  lv_obj_center(s_cal_done_btn);
  lv_obj_set_style_bg_opa(s_cal_done_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_cal_done_btn, 0, 0);
  lv_obj_add_flag(s_cal_done_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_cal_done_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_cal_done_btn, calibration_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Add to focus group so enter/Omega works
  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, s_cal_done_btn);
    lv_group_focus_obj(s_cal_done_btn);
  }
  
  // Set custom back handler
  menu_set_custom_back_handler(calibration_handle_back);
  
  // Start the calibration timer
  s_cal_timer = lv_timer_create(calibration_timer_cb, CAL_SAMPLE_INTERVAL_MS, NULL);
  
  ESP_LOGI(TAG, "Calibration wizard started");
  
  return screen;
}

static void nav_to_calibrate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Calibration", calibration_page_create);
}

// ============================================================================
// Main Settings Expression Page
// ============================================================================

lv_obj_t* menu_page_settings_expression_create(void) {
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Calibrate
  snprintf(s_calibrate_label[buf], sizeof(s_calibrate_label[buf]), "Calibrate");
  s_expr_items[item_count++] = (menu_item_t){
    s_calibrate_label[buf], nav_to_calibrate, NULL, true, MENU_ITEM_KIND_SUBMENU
  };
  
  // TRS Polarity
  expression_polarity_t polarity = expression_get_polarity();
  snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
    "TRS Polarity\n%s", polarity == EXPRESSION_POLARITY_TIP_ADC ? "Tip" : "Ring");
  s_expr_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Switch Type (for sustain/sostenuto pedals)
  pedal_switch_type_t switch_type = expression_get_pedal_switch_type();
  snprintf(s_switch_type_label[buf], sizeof(s_switch_type_label[buf]),
  "Switch Type\n%s", switch_type == PEDAL_SWITCH_NO ? "Normally Open" : "Normally Closed");
  s_expr_items[item_count++] = (menu_item_t){s_switch_type_label[buf], nav_to_switch_type, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Slow Delay
  uint8_t slow_delay = expression_get_slow_delay();
  snprintf(s_slow_delay_label[buf], sizeof(s_slow_delay_label[buf]),
    "Slow Delay\n%u ms", (unsigned)slow_delay);
  s_expr_items[item_count++] = (menu_item_t){s_slow_delay_label[buf], nav_to_slow_delay, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Menu Navigation
  expression_menu_nav_mode_t menu_nav = expression_get_menu_nav_mode();
  const char* nav_str = (menu_nav == EXPR_MENU_NAV_OFF) ? "Off" :
    (menu_nav == EXPR_MENU_NAV_HEEL_MIN) ? "Heel Min" : "Toe Min";
  snprintf(s_menu_nav_label[buf], sizeof(s_menu_nav_label[buf]),
    "Menu Navigation\n%s", nav_str);
  s_expr_items[item_count++] = (menu_item_t){s_menu_nav_label[buf], nav_to_menu_nav, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  return menu_create_page_2line("Expression", s_expr_items, item_count);
}

