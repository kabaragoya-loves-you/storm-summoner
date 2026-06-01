#include "menu.h"
#include "menu_pages.h"
#include "menu_theme.h"
#include "esp_log.h"
#include <stdio.h>

// Forward declarations for index and settings builders (no separate header needed)
extern lv_obj_t* menu_page_index_create(void);

#define TAG "MENU_THEME_PAGE"

static char s_theme_label[32];

static const char* THEME_OPTIONS = "Default\nMonotone\nCVD";

static void theme_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (selected_index >= MENU_THEME_COUNT) selected_index = MENU_THEME_DEFAULT;

  menu_theme_set((menu_theme_t)selected_index);
  ESP_LOGI(TAG, "Theme set to %s", menu_theme_to_string((menu_theme_t)selected_index));

  // Rebuild the pages already in the stack below us so they pick up the new
  // palette when the user navigates back through them.
  // Stack at this point: [0]=Index  [1]=Settings  [2]=Theme-list  [3]=this roller
  // After navigate_back_then_to(2, ...) the stack will be: [0]=Index [1]=Settings [2]=new-Theme
  // Theme-list (depth 2) is about to be popped, so skip it; rebuild Index and Settings.
  menu_rebuild_stack_entry(0, "Menu", menu_page_index_create, "Settings");
  menu_rebuild_stack_entry(1, "Settings", menu_page_settings_create, "Theme");

  menu_navigate_back_then_to(2, "Theme", menu_page_theme_create);
}

static lv_obj_t* theme_roller_create(void) {
  menu_theme_t current = menu_theme_get();
  return menu_create_roller_page("Theme", THEME_OPTIONS, (uint32_t)current,
    theme_confirm_cb, NULL);
}

static void nav_to_theme_roller(void* user_data) {
  (void)user_data;
  menu_navigate_to("Theme", theme_roller_create);
}

lv_obj_t* menu_page_theme_create(void) {
  ESP_LOGI(TAG, "Creating Theme settings page");

  snprintf(s_theme_label, sizeof(s_theme_label), "Theme\n%s",
    menu_theme_to_string(menu_theme_get()));

  static menu_item_t theme_items[] = {
    { s_theme_label, nav_to_theme_roller, NULL, true, MENU_ITEM_KIND_ROLLER }
  };

  return menu_create_page_2line("Theme", theme_items, 1);
}
