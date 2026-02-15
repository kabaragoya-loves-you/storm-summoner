#include "menu.h"
#include "menu_pages.h"
#include "tempo.h"  // LED functions are in tempo.h
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_LED"

// Maximum menu items (enable, mode, sundial)
#define MAX_LED_ITEMS 3

// Label buffers
static char s_enable_label[32];
static char s_mode_label[32];
static char s_sundial_label[32];
static menu_item_t s_led_items[MAX_LED_ITEMS];

// ============================================================================
// LED Enable Roller (Off / On)
// ============================================================================

static const char* LED_ENABLE_OPTIONS = "Off\nOn";

static void enable_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  bool enabled = (selected_index == 1);
  led_set_enabled(enabled);
  ESP_LOGI(TAG, "LED %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "LED", menu_page_led_create);
}

static lv_obj_t* enable_roller_create(void) {
  bool current = led_get_enabled();
  uint32_t initial_index = current ? 1 : 0;
  
  return menu_create_roller_page("LED Enable", LED_ENABLE_OPTIONS,
    initial_index, enable_confirm_cb, NULL);
}

static void nav_to_enable(void* user_data) {
  (void)user_data;
  menu_navigate_to("LED Enable", enable_roller_create);
}

// ============================================================================
// LED Mode Roller (Daylight / Nighttime)
// ============================================================================

static const char* LED_MODE_OPTIONS = "Daylight\nNighttime";

static const char* led_mode_to_string(led_mode_t mode) {
  switch (mode) {
    case LED_MODE_DAYLIGHT:  return "Daylight";
    case LED_MODE_NIGHTTIME: return "Nighttime";
    default: return "Daylight";
  }
}

static uint32_t led_mode_to_index(led_mode_t mode) {
  switch (mode) {
    case LED_MODE_DAYLIGHT:  return 0;
    case LED_MODE_NIGHTTIME: return 1;
    default: return 0;
  }
}

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  led_mode_t mode = (selected_index == 1) ? LED_MODE_NIGHTTIME : LED_MODE_DAYLIGHT;
  led_set_mode(mode);
  ESP_LOGI(TAG, "LED mode set to %s", led_mode_to_string(mode));
  
  menu_navigate_back_then_to(2, "LED", menu_page_led_create);
}

static lv_obj_t* mode_roller_create(void) {
  led_mode_t current = led_get_mode();
  uint32_t initial_index = led_mode_to_index(current);
  
  return menu_create_roller_page("LED Mode", LED_MODE_OPTIONS,
    initial_index, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("LED Mode", mode_roller_create);
}

// ============================================================================
// Sundial Auto-Switch Roller (Off / On)
// ============================================================================

static const char* SUNDIAL_OPTIONS = "Off\nOn";

static void sundial_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  bool enabled = (selected_index == 1);
  led_set_sundial_mode(enabled);
  ESP_LOGI(TAG, "Sundial auto-switch %s", enabled ? "enabled" : "disabled");
  
  menu_navigate_back_then_to(2, "LED", menu_page_led_create);
}

static lv_obj_t* sundial_roller_create(void) {
  bool current = led_get_sundial_mode();
  uint32_t initial_index = current ? 1 : 0;
  
  return menu_create_roller_page("Sundial", SUNDIAL_OPTIONS,
    initial_index, sundial_confirm_cb, NULL);
}

static void nav_to_sundial(void* user_data) {
  (void)user_data;
  menu_navigate_to("Sundial", sundial_roller_create);
}

// ============================================================================
// LED Settings Menu Page
// ============================================================================

lv_obj_t* menu_page_led_create(void) {
  ESP_LOGI(TAG, "Creating LED settings page");
  
  int idx = 0;
  
  // LED Enable
  bool enabled = led_get_enabled();
  snprintf(s_enable_label, sizeof(s_enable_label), "LED Enable\n%s",
    enabled ? "On" : "Off");
  s_led_items[idx++] = (menu_item_t){ s_enable_label, nav_to_enable, NULL, true };
  
  // Only show mode and sundial options when LED is enabled
  if (enabled) {
    // LED Mode
    led_mode_t mode = led_get_mode();
    snprintf(s_mode_label, sizeof(s_mode_label), "Mode\n%s",
      led_mode_to_string(mode));
    s_led_items[idx++] = (menu_item_t){ s_mode_label, nav_to_mode, NULL, true };
    
    // Sundial Auto-Switch
    bool sundial = led_get_sundial_mode();
    snprintf(s_sundial_label, sizeof(s_sundial_label), "Sundial\n%s",
      sundial ? "On" : "Off");
    s_led_items[idx++] = (menu_item_t){ s_sundial_label, nav_to_sundial, NULL, true };
  }
  
  return menu_create_page_2line("LED", s_led_items, idx);
}
