#include "menu.h"
#include "menu_pages.h"
#include "touch.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_TOUCH"

// Forward declarations for roller page builders
static lv_obj_t* stuck_timeout_roller_create(void);
static lv_obj_t* idle_calib_roller_create(void);
static lv_obj_t* menu_hold_roller_create(void);

// External declaration for working module message setter
extern void working_set_message(const char *msg);

// Label buffers
static char s_calibrate_label[32];
static char s_stuck_timeout_label[32];
static char s_idle_calib_label[32];
static char s_menu_hold_label[32];
static menu_item_t s_touch_items[4];

// ============================================================================
// Calibrate Action
// ============================================================================

static void calibrate_complete_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  // Return to touch menu
  menu_navigate_back();
}

static void calibrate_deferred_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  
  // Perform calibration
  ESP_LOGI(TAG, "Starting touch calibration...");
  force_touch_calibration();
  ESP_LOGI(TAG, "Calibration complete");
  
  // Brief delay to show completion, then return to menu
  lv_timer_t *complete_timer = lv_timer_create(calibrate_complete_cb, 500, NULL);
  if (complete_timer) lv_timer_set_repeat_count(complete_timer, 1);
}

static void action_calibrate(void* user_data) {
  (void)user_data;
  
  // Show working screen with calibrating message
  working_set_message("Calibrating...");
  ui_set_draw_module(&working_module);
  
  // Defer calibration to let LVGL render the working screen
  lv_timer_t *timer = lv_timer_create(calibrate_deferred_cb, 100, NULL);
  if (timer) lv_timer_set_repeat_count(timer, 1);
}

// ============================================================================
// Stuck Timeout Roller
// ============================================================================

// Options: Disable, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
static const char* STUCK_TIMEOUT_OPTIONS = 
  "Disable\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20";

static void stuck_timeout_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint32_t timeout_ms;
  if (selected_index == 0) {
    timeout_ms = 0;  // Disable
  } else {
    // Index 1 = 5s, index 2 = 6s, etc.
    timeout_ms = (selected_index + 4) * 1000;
  }
  
  touch_set_stuck_timeout_ms(timeout_ms);
  ESP_LOGI(TAG, "Stuck timeout set to %lu ms", (unsigned long)timeout_ms);
  
  menu_navigate_back_then_to(2, "Touch", menu_page_touch_create);
}

static uint32_t stuck_timeout_to_index(uint32_t timeout_ms) {
  if (timeout_ms == 0) return 0;  // Disable
  uint32_t seconds = timeout_ms / 1000;
  if (seconds < 5) return 0;  // Below minimum, treat as disabled
  if (seconds > 20) return 16;  // Above maximum, clamp to 20s
  return seconds - 4;  // 5s = index 1, 6s = index 2, etc.
}

static const char* stuck_timeout_to_string(uint32_t timeout_ms) {
  if (timeout_ms == 0) return "Disabled";
  static char buf[16];
  snprintf(buf, sizeof(buf), "%lu sec", (unsigned long)(timeout_ms / 1000));
  return buf;
}

static lv_obj_t* stuck_timeout_roller_create(void) {
  uint32_t current_ms = touch_get_stuck_timeout_ms();
  uint32_t initial_index = stuck_timeout_to_index(current_ms);
  
  return menu_create_roller_page("Stuck Timeout", STUCK_TIMEOUT_OPTIONS,
    initial_index, stuck_timeout_confirm_cb, NULL);
}

static void nav_to_stuck_timeout(void* user_data) {
  (void)user_data;
  menu_navigate_to("Stuck Timeout", stuck_timeout_roller_create);
}

// ============================================================================
// Idle Calibration Roller
// ============================================================================

// Options: Disable, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60 (minutes)
static const char* IDLE_CALIB_OPTIONS = 
  "Disable\n10\n15\n20\n25\n30\n35\n40\n45\n50\n55\n60";

static void idle_calib_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  uint32_t interval_ms;
  if (selected_index == 0) {
    interval_ms = 0;  // Disable
  } else {
    // Index 1 = 10min, index 2 = 15min, etc. (5 min increments starting at 10)
    uint32_t minutes = 10 + (selected_index - 1) * 5;
    interval_ms = minutes * 60 * 1000;
  }
  
  touch_set_idle_calibration_interval_ms(interval_ms);
  ESP_LOGI(TAG, "Idle calibration interval set to %lu ms", (unsigned long)interval_ms);
  
  menu_navigate_back_then_to(2, "Touch", menu_page_touch_create);
}

static uint32_t idle_calib_to_index(uint32_t interval_ms) {
  if (interval_ms == 0) return 0;  // Disable
  uint32_t minutes = interval_ms / 60000;
  if (minutes < 10) return 0;  // Below minimum, treat as disabled
  if (minutes > 60) return 11;  // Above maximum, clamp to 60m
  // 10min = index 1, 15min = index 2, etc.
  return 1 + (minutes - 10) / 5;
}

static const char* idle_calib_to_string(uint32_t interval_ms) {
  if (interval_ms == 0) return "Disabled";
  static char buf[16];
  snprintf(buf, sizeof(buf), "%lu min", (unsigned long)(interval_ms / 60000));
  return buf;
}

static lv_obj_t* idle_calib_roller_create(void) {
  uint32_t current_ms = touch_get_idle_calibration_interval_ms();
  uint32_t initial_index = idle_calib_to_index(current_ms);
  
  return menu_create_roller_page("Idle Calibration", IDLE_CALIB_OPTIONS,
    initial_index, idle_calib_confirm_cb, NULL);
}

static void nav_to_idle_calib(void* user_data) {
  (void)user_data;
  menu_navigate_to("Idle Calibration", idle_calib_roller_create);
}

// ============================================================================
// Menu Hold Time Roller
// ============================================================================

// Options: 0.8s to 2.5s in 0.1s increments (18 options)
static const char* MENU_HOLD_OPTIONS = 
  "0.8s\n0.9s\n1.0s\n1.1s\n1.2s\n1.3s\n1.4s\n1.5s\n1.6s\n"
  "1.7s\n1.8s\n1.9s\n2.0s\n2.1s\n2.2s\n2.3s\n2.4s\n2.5s";

static void menu_hold_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  // Index 0 = 800ms, index 1 = 900ms, etc.
  uint32_t hold_ms = 800 + (selected_index * 100);
  
  ui_set_button13_long_press_ms(hold_ms);
  ESP_LOGI(TAG, "Menu hold time set to %lu ms", (unsigned long)hold_ms);
  
  menu_navigate_back_then_to(2, "Touch", menu_page_touch_create);
}

static uint32_t menu_hold_to_index(uint32_t hold_ms) {
  if (hold_ms < 800) return 0;  // Below minimum, clamp to 800ms
  if (hold_ms > 2500) return 17;  // Above maximum, clamp to 2500ms
  return (hold_ms - 800) / 100;
}

static const char* menu_hold_to_string(uint32_t hold_ms) {
  static char buf[16];
  snprintf(buf, sizeof(buf), "%lu.%lu sec",
    (unsigned long)(hold_ms / 1000),
    (unsigned long)((hold_ms % 1000) / 100));
  return buf;
}

static lv_obj_t* menu_hold_roller_create(void) {
  uint32_t current_ms = ui_get_button13_long_press_ms();
  uint32_t initial_index = menu_hold_to_index(current_ms);
  
  return menu_create_roller_page("Menu Hold Time", MENU_HOLD_OPTIONS,
    initial_index, menu_hold_confirm_cb, NULL);
}

static void nav_to_menu_hold(void* user_data) {
  (void)user_data;
  menu_navigate_to("Menu Hold Time", menu_hold_roller_create);
}

// ============================================================================
// Touch Settings Menu Page
// ============================================================================

lv_obj_t* menu_page_touch_create(void) {
  ESP_LOGI(TAG, "Creating touch settings page");
  
  int idx = 0;
  
  // Calibrate (no value on second line)
  snprintf(s_calibrate_label, sizeof(s_calibrate_label), "Calibrate\n");
  s_touch_items[idx++] = (menu_item_t){
    s_calibrate_label, action_calibrate, NULL, false, MENU_ITEM_KIND_ACTION
  };
  
  // Stuck Timeout with current value
  uint32_t stuck_ms = touch_get_stuck_timeout_ms();
  snprintf(s_stuck_timeout_label, sizeof(s_stuck_timeout_label), "Stuck Timeout\n%s",
    stuck_timeout_to_string(stuck_ms));
  s_touch_items[idx++] = (menu_item_t){ s_stuck_timeout_label, nav_to_stuck_timeout, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Idle Calibration with current value
  uint32_t idle_ms = touch_get_idle_calibration_interval_ms();
  snprintf(s_idle_calib_label, sizeof(s_idle_calib_label), "Idle Calibration\n%s",
    idle_calib_to_string(idle_ms));
  s_touch_items[idx++] = (menu_item_t){ s_idle_calib_label, nav_to_idle_calib, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Menu Hold Time with current value
  uint32_t hold_ms = ui_get_button13_long_press_ms();
  snprintf(s_menu_hold_label, sizeof(s_menu_hold_label), "Menu Hold Time\n%s",
    menu_hold_to_string(hold_ms));
  s_touch_items[idx++] = (menu_item_t){ s_menu_hold_label, nav_to_menu_hold, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  return menu_create_page_2line("Touch", s_touch_items, idx);
}
