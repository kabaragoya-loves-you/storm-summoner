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

#define MAX_BUTTON_ITEMS 7
static menu_item_t s_button_items[MAX_BUTTON_ITEMS];

static char s_debounce_label[LABEL_BUFFER_SETS][32];
static char s_debounce_mode_label[LABEL_BUFFER_SETS][32];
static char s_debounce_release_label[LABEL_BUFFER_SETS][32];
static char s_chord_label[LABEL_BUFFER_SETS][32];
static char s_longpress_label[LABEL_BUFFER_SETS][32];
static char s_glitch_mode_label[LABEL_BUFFER_SETS][32];
static char s_glitch_window_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// Static option strings for rollers
static char s_debounce_options[512];   // 0-100 with 1ms step
static char s_debounce_release_options[512]; // 0-100 with 1ms step
static char s_chord_options[512];      // 0-500 with 10ms step
static char s_longpress_options[512];  // 100-5000 with 100ms step
static char s_glitch_window_options[256]; // 100-4000ns with 100ns step

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

static void build_debounce_release_options(void) {
  // 0-100 with 1ms step (same as debounce)
  char* p = s_debounce_release_options;
  size_t remaining = sizeof(s_debounce_release_options);
  
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

static void build_glitch_window_options(void) {
  // 100-4000ns with 100ns step
  char* p = s_glitch_window_options;
  size_t remaining = sizeof(s_glitch_window_options);
  
  for (int i = 100; i <= 4000; i += 100) {
    int written;
    if (i == 100) {
      written = snprintf(p, remaining, "%d ns", i);
    } else {
      written = snprintf(p, remaining, "\n%d ns", i);
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
// Debounce Mode Roller (Symmetric / Asymmetric)
// ============================================================================

static const char* DEBOUNCE_MODE_OPTIONS = "Symmetric\nAsymmetric";

static const char* debounce_mode_to_string(uint8_t mode) {
  return (mode == BUTTON_DEBOUNCE_MODE_ASYMMETRIC) ? "Asymmetric" : "Symmetric";
}

static void debounce_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t mode = (selected_index == 1) ?
    BUTTON_DEBOUNCE_MODE_ASYMMETRIC : BUTTON_DEBOUNCE_MODE_SYMMETRIC;
  buttons_set_debounce_mode(mode);
  ESP_LOGI(TAG, "Debounce mode set to: %s", debounce_mode_to_string(mode));
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* debounce_mode_roller_create(void) {
  uint8_t current = buttons_get_debounce_mode();
  uint32_t initial_index = (current == BUTTON_DEBOUNCE_MODE_ASYMMETRIC) ? 1 : 0;
  
  return menu_create_roller_page("Debounce Mode", DEBOUNCE_MODE_OPTIONS,
    initial_index, debounce_mode_confirm_cb, NULL);
}

static void nav_to_debounce_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Debounce Mode", debounce_mode_roller_create);
}

// ============================================================================
// Debounce Release Roller (0-100ms, only for asymmetric mode)
// ============================================================================

static void debounce_release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint16_t release_ms = (uint16_t)selected_index;
  buttons_set_debounce_release(release_ms);
  ESP_LOGI(TAG, "Debounce release set to: %u ms", (unsigned)release_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* debounce_release_roller_create(void) {
  build_debounce_release_options();
  
  uint16_t current = buttons_get_debounce_release();
  if (current > 100) current = 100;
  
  return menu_create_roller_page("Release Debounce", s_debounce_release_options,
    current, debounce_release_confirm_cb, NULL);
}

static void nav_to_debounce_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release Debounce", debounce_release_roller_create);
}

// ============================================================================
// Glitch Filter Mode Roller (None / Simple / Flex)
// ============================================================================

static const char* GLITCH_MODE_OPTIONS = "None\nSimple\nFlex";

static const char* glitch_mode_to_string(uint8_t mode) {
  switch (mode) {
    case BUTTON_GLITCH_FILTER_MODE_SIMPLE: return "Simple";
    case BUTTON_GLITCH_FILTER_MODE_FLEX:   return "Flex";
    default: return "None";
  }
}

static void glitch_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t mode = (uint8_t)selected_index;
  uint32_t window = buttons_get_glitch_filter_window_ns();
  // Flex mode requires window in 100-4000ns range; use default if invalid
  if (mode == BUTTON_GLITCH_FILTER_MODE_FLEX && (window < 100 || window > 4000)) {
    window = 1000; // Sensible default: 1µs
  }
  buttons_set_glitch_filter(mode, window);
  ESP_LOGI(TAG, "Glitch filter mode set to: %s", glitch_mode_to_string(mode));
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* glitch_mode_roller_create(void) {
  uint8_t current = buttons_get_glitch_filter_mode();
  if (current > 2) current = 0;
  
  return menu_create_roller_page("Glitch Filter", GLITCH_MODE_OPTIONS,
    current, glitch_mode_confirm_cb, NULL);
}

static void nav_to_glitch_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Glitch Filter", glitch_mode_roller_create);
}

// ============================================================================
// Glitch Filter Window Roller (100-4000ns, only for flex mode)
// ============================================================================

static void glitch_window_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // (Index + 1) * 100 = ns value (100, 200, ... 4000)
  uint32_t window_ns = (selected_index + 1) * 100;
  uint8_t mode = buttons_get_glitch_filter_mode();
  buttons_set_glitch_filter(mode, window_ns);
  ESP_LOGI(TAG, "Glitch filter window set to: %lu ns", (unsigned long)window_ns);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Buttons", menu_page_buttons_create);
}

static lv_obj_t* glitch_window_roller_create(void) {
  build_glitch_window_options();
  
  uint32_t current = buttons_get_glitch_filter_window_ns();
  // Convert ns to index: (ns / 100) - 1
  uint32_t current_idx = 0;
  if (current >= 100) {
    current_idx = (current / 100) - 1;
  }
  if (current_idx > 39) current_idx = 39;
  
  return menu_create_roller_page("Glitch Window", s_glitch_window_options,
    current_idx, glitch_window_confirm_cb, NULL);
}

static void nav_to_glitch_window(void* user_data) {
  (void)user_data;
  menu_navigate_to("Glitch Window", glitch_window_roller_create);
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
  s_button_items[item_count++] = (menu_item_t){
    s_debounce_label[buf], nav_to_debounce, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Debounce Mode
  uint8_t debounce_mode = buttons_get_debounce_mode();
  snprintf(s_debounce_mode_label[buf], sizeof(s_debounce_mode_label[buf]),
    "Debounce Mode\n%s", debounce_mode_to_string(debounce_mode));
  s_button_items[item_count++] = (menu_item_t){
    s_debounce_mode_label[buf], nav_to_debounce_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Debounce Release (only shown when mode == asymmetric)
  if (debounce_mode == BUTTON_DEBOUNCE_MODE_ASYMMETRIC) {
    uint16_t release = buttons_get_debounce_release();
    snprintf(s_debounce_release_label[buf], sizeof(s_debounce_release_label[buf]),
      "Release Debounce\n%u ms", (unsigned)release);
    s_button_items[item_count++] = (menu_item_t){
      s_debounce_release_label[buf], nav_to_debounce_release, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  // Chord Window
  uint16_t chord = buttons_get_chord_window();
  snprintf(s_chord_label[buf], sizeof(s_chord_label[buf]),
    "Chord Window\n%u ms", (unsigned)chord);
  s_button_items[item_count++] = (menu_item_t){
    s_chord_label[buf], nav_to_chord, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Long Press
  uint16_t longpress = buttons_get_long_press_threshold();
  snprintf(s_longpress_label[buf], sizeof(s_longpress_label[buf]),
    "Long Press\n%u ms", (unsigned)longpress);
  s_button_items[item_count++] = (menu_item_t){
    s_longpress_label[buf], nav_to_longpress, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Glitch Filter Mode
  uint8_t glitch_mode = buttons_get_glitch_filter_mode();
  snprintf(s_glitch_mode_label[buf], sizeof(s_glitch_mode_label[buf]),
    "Glitch Filter\n%s", glitch_mode_to_string(glitch_mode));
  s_button_items[item_count++] = (menu_item_t){
    s_glitch_mode_label[buf], nav_to_glitch_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Glitch Filter Window (only shown when mode == flex)
  if (glitch_mode == BUTTON_GLITCH_FILTER_MODE_FLEX) {
    uint32_t glitch_window = buttons_get_glitch_filter_window_ns();
    snprintf(s_glitch_window_label[buf], sizeof(s_glitch_window_label[buf]),
      "Glitch Window\n%lu ns", (unsigned long)glitch_window);
    s_button_items[item_count++] = (menu_item_t){
      s_glitch_window_label[buf], nav_to_glitch_window, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  return menu_create_page_2line("Buttons", s_button_items, item_count);
}
