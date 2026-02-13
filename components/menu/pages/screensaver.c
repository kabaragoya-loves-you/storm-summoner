#include "menu.h"
#include "menu_pages.h"
#include "screensaver.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_SCREENSAVER"

// Forward declarations for roller page builders
static lv_obj_t* delay_roller_create(void);
static lv_obj_t* mode_roller_create(void);

// Label buffers
static char s_delay_label[32];
static char s_mode_label[32];
static menu_item_t s_screensaver_items[2];

// ============================================================================
// Delay Roller
// ============================================================================

// Options: Disable, 5, 10, 20, 30, 60 (minutes)
// Stored as seconds: 0, 300, 600, 1200, 1800, 3600
static const char* DELAY_OPTIONS = "Disable\n5 min\n10 min\n20 min\n30 min\n60 min";

// Delay values in seconds for each index
static const uint16_t DELAY_VALUES[] = { 0, 300, 600, 1200, 1800, 3600 };
static const int DELAY_VALUE_COUNT = sizeof(DELAY_VALUES) / sizeof(DELAY_VALUES[0]);

static void delay_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
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
  
  menu_navigate_back_then_to(2, "Screensaver", menu_page_screensaver_create);
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
  
  return menu_create_roller_page("Delay", DELAY_OPTIONS,
    initial_index, delay_confirm_cb, NULL);
}

static void nav_to_delay(void* user_data) {
  (void)user_data;
  menu_navigate_to("Delay", delay_roller_create);
}

// ============================================================================
// Mode Roller
// ============================================================================

static const char* MODE_OPTIONS = "Starfield\nElite\nPlasma";

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  screensaver_mode_t mode;
  switch (selected_index) {
    case 0:  mode = SCREENSAVER_MODE_STARFIELD; break;
    case 1:  mode = SCREENSAVER_MODE_ELITE; break;
    default: mode = SCREENSAVER_MODE_PLASMA; break;
  }
  
  screensaver_set_mode(mode);
  ESP_LOGI(TAG, "Screensaver mode set to index %lu", (unsigned long)selected_index);
  
  menu_navigate_back_then_to(2, "Screensaver", menu_page_screensaver_create);
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
  
  return menu_create_roller_page("Mode", MODE_OPTIONS,
    initial_index, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Screensaver Settings Menu Page
// ============================================================================

lv_obj_t* menu_page_screensaver_create(void) {
  ESP_LOGI(TAG, "Creating screensaver settings page");
  
  int idx = 0;
  
  // Delay with current value
  uint16_t delay = screensaver_get_delay();
  snprintf(s_delay_label, sizeof(s_delay_label), "Delay\n%s",
    delay_to_string(delay));
  s_screensaver_items[idx++] = (menu_item_t){ s_delay_label, nav_to_delay, NULL, true };
  
  // Mode with current value (only show if screensaver is enabled)
  if (delay > 0) {
    screensaver_mode_t mode = screensaver_get_mode();
    snprintf(s_mode_label, sizeof(s_mode_label), "Mode\n%s",
      mode_to_string(mode));
    s_screensaver_items[idx++] = (menu_item_t){ s_mode_label, nav_to_mode, NULL, true };
  }
  
  return menu_create_page_2line("Screensaver", s_screensaver_items, idx);
}
