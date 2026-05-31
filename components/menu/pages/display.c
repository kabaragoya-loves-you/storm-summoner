#include "menu.h"
#include "menu_pages.h"
#include "display_console.h"
#include "screensaver.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_DISPLAY"

// Forward declarations
lv_obj_t* menu_page_display_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_DISPLAY_ITEMS 3
static menu_item_t s_display_items[MAX_DISPLAY_ITEMS];

static char s_brightness_label[LABEL_BUFFER_SETS][32];
static char s_delay_label[LABEL_BUFFER_SETS][32];
static char s_mode_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// ============================================================================
// Brightness Roller (0-100% in 10% increments)
// ============================================================================

static const char* BRIGHTNESS_OPTIONS =
  "0%\n10%\n20%\n30%\n40%\n50%\n60%\n70%\n80%\n90%\n100%";

static void brightness_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index * 10 = percent value
  uint8_t brightness = (uint8_t)(selected_index * 10);
  display_set_brightness(brightness);
  ESP_LOGI(TAG, "Brightness set to: %u%%", (unsigned)brightness);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Display", menu_page_display_create);
}

static lv_obj_t* brightness_roller_create(void) {
  uint8_t current = display_get_brightness();
  uint32_t current_idx = current / 10;
  if (current_idx > 10) current_idx = 10;
  
  return menu_create_roller_page("Brightness", BRIGHTNESS_OPTIONS,
    current_idx, brightness_confirm_cb, NULL);
}

static void nav_to_brightness(void* user_data) {
  (void)user_data;
  menu_navigate_to("Brightness", brightness_roller_create);
}

// ============================================================================
// Screensaver Delay Roller
// ============================================================================

// Options: Disable, 5, 10, 20, 30, 60 (minutes)
// Stored as seconds: 0, 300, 600, 1200, 1800, 3600
static const char* DELAY_OPTIONS = "Disable\n5 min\n10 min\n20 min\n30 min\n60 min";

// Delay values in seconds for each index
static const uint16_t DELAY_VALUES[] = { 0, 300, 600, 1200, 1800, 3600 };
static const int DELAY_VALUE_COUNT = sizeof(DELAY_VALUES) / sizeof(DELAY_VALUES[0]);

static void delay_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint16_t delay_seconds = 0;
  if (selected_index < DELAY_VALUE_COUNT) {
    delay_seconds = DELAY_VALUES[selected_index];
  }
  
  screensaver_set_delay(delay_seconds);
  
  if (delay_seconds == 0) {
    ESP_LOGI(TAG, "Screensaver disabled");
  } else {
    ESP_LOGI(TAG, "Screensaver delay set to %u seconds (%u min)",
      delay_seconds, delay_seconds / 60);
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Display", menu_page_display_create);
}

static uint32_t delay_to_index(uint16_t delay_seconds) {
  for (int i = 0; i < DELAY_VALUE_COUNT; i++) {
    if (DELAY_VALUES[i] == delay_seconds) return i;
  }
  // Find closest match if exact value not found
  if (delay_seconds == 0) return 0;
  if (delay_seconds <= 300) return 1;
  if (delay_seconds <= 600) return 2;
  if (delay_seconds <= 1200) return 3;
  if (delay_seconds <= 1800) return 4;
  return 5;
}

static const char* delay_to_string(uint16_t delay_seconds) {
  if (delay_seconds == 0) return "Disabled";
  static char buf[16];
  snprintf(buf, sizeof(buf), "%u min", delay_seconds / 60);
  return buf;
}

static lv_obj_t* delay_roller_create(void) {
  uint16_t current = screensaver_get_delay();
  uint32_t initial_index = delay_to_index(current);
  
  return menu_create_roller_page("Screensaver", DELAY_OPTIONS,
    initial_index, delay_confirm_cb, NULL);
}

static void nav_to_delay(void* user_data) {
  (void)user_data;
  menu_navigate_to("Screensaver", delay_roller_create);
}

// ============================================================================
// Screensaver Mode Roller
// ============================================================================

static const char* MODE_OPTIONS = "Starfield\nElite\nPlasma";

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  screensaver_mode_t mode;
  switch (selected_index) {
    case 0:  mode = SCREENSAVER_MODE_STARFIELD; break;
    case 1:  mode = SCREENSAVER_MODE_ELITE; break;
    default: mode = SCREENSAVER_MODE_PLASMA; break;
  }
  
  screensaver_set_mode(mode);
  ESP_LOGI(TAG, "Screensaver mode set to index %lu", (unsigned long)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Display", menu_page_display_create);
}

static uint32_t mode_to_index(screensaver_mode_t mode) {
  switch (mode) {
    case SCREENSAVER_MODE_STARFIELD: return 0;
    case SCREENSAVER_MODE_ELITE:     return 1;
    case SCREENSAVER_MODE_PLASMA:    return 2;
    default: return 0;
  }
}

static const char* mode_to_string(screensaver_mode_t mode) {
  switch (mode) {
    case SCREENSAVER_MODE_STARFIELD: return "Starfield";
    case SCREENSAVER_MODE_ELITE:     return "Elite";
    case SCREENSAVER_MODE_PLASMA:    return "Plasma";
    default: return "Starfield";
  }
}

static lv_obj_t* mode_roller_create(void) {
  screensaver_mode_t current = screensaver_get_mode();
  uint32_t initial_index = mode_to_index(current);
  
  return menu_create_roller_page("Saver Mode", MODE_OPTIONS,
    initial_index, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Saver Mode", mode_roller_create);
}

// ============================================================================
// Main Display Settings Page
// ============================================================================

lv_obj_t* menu_page_display_create(void) {
  ESP_LOGI(TAG, "Creating Display settings page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Brightness (0-100%)
  uint8_t brightness = display_get_brightness();
  snprintf(s_brightness_label[buf], sizeof(s_brightness_label[buf]),
    "Brightness\n%u%%", (unsigned)brightness);
  s_display_items[item_count++] = (menu_item_t){
    s_brightness_label[buf], nav_to_brightness, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Screensaver Delay
  uint16_t delay = screensaver_get_delay();
  snprintf(s_delay_label[buf], sizeof(s_delay_label[buf]),
    "Screensaver\n%s", delay_to_string(delay));
  s_display_items[item_count++] = (menu_item_t){
    s_delay_label[buf], nav_to_delay, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Screensaver Mode (only shown when screensaver is enabled)
  if (delay > 0) {
    screensaver_mode_t mode = screensaver_get_mode();
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]),
      "Saver Mode\n%s", mode_to_string(mode));
    s_display_items[item_count++] = (menu_item_t){
      s_mode_label[buf], nav_to_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  return menu_create_page_2line("Display", s_display_items, item_count);
}
