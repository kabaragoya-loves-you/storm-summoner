#include "menu.h"
#include "menu_pages.h"
#include "buttons.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_SETTINGS_BUTTONS"

// Forward declarations
lv_obj_t* menu_page_buttons_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_BUTTON_ITEMS 3
static menu_item_t s_button_items[MAX_BUTTON_ITEMS];

static char s_debounce_label[LABEL_BUFFER_SETS][32];
static char s_chord_label[LABEL_BUFFER_SETS][32];
static char s_longpress_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// Static option strings for rollers
static char s_debounce_options[512];   // 0-100 with 1ms step
static char s_chord_options[512];      // 0-500 with 10ms step
static char s_longpress_options[512];  // 100-5000 with 100ms step

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void build_debounce_options(void) {
  // 0-100 with 1ms step
  char* p = s_debounce_options;
  size_t remaining = sizeof(s_debounce_options);
  
  for (int i = 0; i <= 100; i++) {
    int written;
    if (i == 0) {
      written = snprintf(p, remaining, "%d ms", i);
    } else {
      written = snprintf(p, remaining, "\n%d ms", i);
    }
    if (written > 0 && (size_t)written < remaining) {
      p += written;
      remaining -= written;
    }
  }
}

static void build_chord_options(void) {
  // 0-500 with 10ms step
  char* p = s_chord_options;
  size_t remaining = sizeof(s_chord_options);
  
  for (int i = 0; i <= 500; i += 10) {
    int written;
    if (i == 0) {
      written = snprintf(p, remaining, "%d ms", i);
    } else {
      written = snprintf(p, remaining, "\n%d ms", i);
    }
    if (written > 0 && (size_t)written < remaining) {
      p += written;
      remaining -= written;
    }
  }
}

static void build_longpress_options(void) {
  // 100-5000 with 100ms step
  char* p = s_longpress_options;
  size_t remaining = sizeof(s_longpress_options);
  
  for (int i = 100; i <= 5000; i += 100) {
    int written;
    if (i == 100) {
      written = snprintf(p, remaining, "%d ms", i);
    } else {
      written = snprintf(p, remaining, "\n%d ms", i);
    }
    if (written > 0 && (size_t)written < remaining) {
      p += written;
      remaining -= written;
    }
  }
}

// ============================================================================
// Debounce Delay Roller
// ============================================================================

static void debounce_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index directly maps to ms value (0-100)
  uint16_t debounce_ms = (uint16_t)selected_index;
  buttons_set_debounce(debounce_ms);
  ESP_LOGI(TAG, "Debounce set to: %u ms", (unsigned)debounce_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* debounce_roller_create(void) {
  build_debounce_options();
  
  uint16_t current = buttons_get_debounce();
  if (current > 100) current = 100;
  
  return menu_create_roller_page("Debounce Delay", s_debounce_options, current, debounce_confirm_cb, NULL);
}

static void nav_to_debounce(void* user_data) {
  (void)user_data;
  menu_navigate_to("Debounce Delay", debounce_roller_create);
}

// ============================================================================
// Chord Window Roller
// ============================================================================

static void chord_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index * 10 = ms value (0, 10, 20, ... 500)
  uint16_t chord_ms = (uint16_t)(selected_index * 10);
  buttons_set_chord_window(chord_ms);
  ESP_LOGI(TAG, "Chord window set to: %u ms", (unsigned)chord_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* chord_roller_create(void) {
  build_chord_options();
  
  uint16_t current = buttons_get_chord_window();
  uint32_t current_idx = current / 10;
  if (current_idx > 50) current_idx = 50;
  
  return menu_create_roller_page("Chord Window", s_chord_options, current_idx, chord_confirm_cb, NULL);
}

static void nav_to_chord(void* user_data) {
  (void)user_data;
  menu_navigate_to("Chord Window", chord_roller_create);
}

// ============================================================================
// Long Press Roller
// ============================================================================

static void longpress_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // (Index + 1) * 100 = ms value (100, 200, ... 5000)
  uint16_t longpress_ms = (uint16_t)((selected_index + 1) * 100);
  buttons_set_long_press_threshold(longpress_ms);
  ESP_LOGI(TAG, "Long press set to: %u ms", (unsigned)longpress_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* longpress_roller_create(void) {
  build_longpress_options();
  
  uint16_t current = buttons_get_long_press_threshold();
  // Convert ms to index: (ms / 100) - 1
  uint32_t current_idx = 0;
  if (current >= 100) {
    current_idx = (current / 100) - 1;
  }
  if (current_idx > 49) current_idx = 49;
  
  return menu_create_roller_page("Long Press", s_longpress_options, current_idx, longpress_confirm_cb, NULL);
}

static void nav_to_longpress(void* user_data) {
  (void)user_data;
  menu_navigate_to("Long Press", longpress_roller_create);
}

// ============================================================================
// Main Settings Buttons Page
// ============================================================================

lv_obj_t* menu_page_buttons_create(void) {
  ESP_LOGI(TAG, "Creating Settings Buttons page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Debounce Delay
  uint16_t debounce = buttons_get_debounce();
  snprintf(s_debounce_label[buf], sizeof(s_debounce_label[buf]),
    "Debounce Delay\n%u ms", (unsigned)debounce);
  s_button_items[item_count++] = (menu_item_t){s_debounce_label[buf], nav_to_debounce, NULL, true};
  
  // Chord Window
  uint16_t chord = buttons_get_chord_window();
  snprintf(s_chord_label[buf], sizeof(s_chord_label[buf]),
    "Chord Window\n%u ms", (unsigned)chord);
  s_button_items[item_count++] = (menu_item_t){s_chord_label[buf], nav_to_chord, NULL, true};
  
  // Long Press
  uint16_t longpress = buttons_get_long_press_threshold();
  snprintf(s_longpress_label[buf], sizeof(s_longpress_label[buf]),
    "Long Press\n%u ms", (unsigned)longpress);
  s_button_items[item_count++] = (menu_item_t){s_longpress_label[buf], nav_to_longpress, NULL, true};
  
  return menu_create_page_2line("Buttons", s_button_items, item_count);
}
