#include "menu.h"
#include "menu_pages.h"
#include "bump.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_BUMP"

// Forward declarations
lv_obj_t* menu_page_bump_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_BUMP_ITEMS 2
static menu_item_t s_bump_items[MAX_BUMP_ITEMS];

static char s_sensitivity_label[LABEL_BUFFER_SETS][32];
static char s_debounce_label[LABEL_BUFFER_SETS][32];

static bool s_callback_in_progress = false;

// Static option strings for rollers
static char s_debounce_options[512];   // 0-500ms with 10ms step

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void build_debounce_options(void) {
  // 0-500ms with 10ms step
  char* p = s_debounce_options;
  size_t remaining = sizeof(s_debounce_options);
  
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

// ============================================================================
// Sensitivity Level Roller (1-10)
// ============================================================================

static const char* SENSITIVITY_OPTIONS =
  "1 (very sensitive)\n2\n3\n4\n5 (default)\n6\n7\n8\n9\n10 (insensitive)";

static const char* sensitivity_to_string(uint8_t level) {
  static char buf[8];
  snprintf(buf, sizeof(buf), "%u", (unsigned)level);
  return buf;
}

static void sensitivity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index 0 = level 1, index 9 = level 10
  uint8_t level = (uint8_t)(selected_index + 1);
  bump_set_sensitivity_level(level);
  ESP_LOGI(TAG, "Sensitivity level set to: %u", (unsigned)level);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Bump", menu_page_bump_create);
}

static lv_obj_t* sensitivity_roller_create(void) {
  uint8_t current = bump_get_sensitivity_level();
  if (current < 1) current = 1;
  if (current > 10) current = 10;
  uint32_t initial_index = current - 1;
  
  return menu_create_roller_page("Sensitivity", SENSITIVITY_OPTIONS,
    initial_index, sensitivity_confirm_cb, NULL);
}

static void nav_to_sensitivity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Sensitivity", sensitivity_roller_create);
}

// ============================================================================
// Debounce Roller (0-500ms)
// ============================================================================

static void debounce_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index * 10 = ms value
  uint32_t debounce_ms = selected_index * 10;
  bump_set_debounce(debounce_ms);
  ESP_LOGI(TAG, "Debounce set to: %lu ms", (unsigned long)debounce_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Bump", menu_page_bump_create);
}

static lv_obj_t* debounce_roller_create(void) {
  build_debounce_options();
  
  uint32_t current = bump_get_debounce();
  uint32_t current_idx = current / 10;
  if (current_idx > 50) current_idx = 50;
  
  return menu_create_roller_page("Debounce", s_debounce_options,
    current_idx, debounce_confirm_cb, NULL);
}

static void nav_to_debounce(void* user_data) {
  (void)user_data;
  menu_navigate_to("Debounce", debounce_roller_create);
}

// ============================================================================
// Main Bump Settings Page
// ============================================================================

lv_obj_t* menu_page_bump_create(void) {
  ESP_LOGI(TAG, "Creating Bump settings page");
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Sensitivity Level (1-10)
  uint8_t sensitivity = bump_get_sensitivity_level();
  snprintf(s_sensitivity_label[buf], sizeof(s_sensitivity_label[buf]),
    "Sensitivity\n%s", sensitivity_to_string(sensitivity));
  s_bump_items[item_count++] = (menu_item_t){
    s_sensitivity_label[buf], nav_to_sensitivity, NULL, true};
  
  // Debounce (ms)
  uint32_t debounce = bump_get_debounce();
  snprintf(s_debounce_label[buf], sizeof(s_debounce_label[buf]),
    "Debounce\n%lu ms", (unsigned long)debounce);
  s_bump_items[item_count++] = (menu_item_t){
    s_debounce_label[buf], nav_to_debounce, NULL, true};
  
  return menu_create_page_2line("Bump", s_bump_items, item_count);
}
