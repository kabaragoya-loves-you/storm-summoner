#include "menu.h"
#include "menu_pages.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_UI"

static void show_info(void* user_data) {
  (void)user_data;
  const char* mode_str;
  switch (ui_get_app_mode()) {
    case APP_MODE_PERFORMANCE: mode_str = "Performance"; break;
    case APP_MODE_PROGRAMMING: mode_str = "Programming"; break;
    case APP_MODE_SCREENSAVER: mode_str = "Screensaver"; break;
    default: mode_str = "Unknown"; break;
  }
  
  char info_text[128];
  snprintf(info_text, sizeof(info_text), "UI\nApp mode: %s", mode_str);
  
  menu_navigate_to_info("UI Info", info_text);
}

lv_obj_t* menu_page_ui_create(void) {
  ESP_LOGI(TAG, "Creating UI page");
  
  static menu_item_t ui_items[] = {
    { "Info", show_info, NULL, false }
  };
  
  return menu_create_page("UI", ui_items, 
    sizeof(ui_items) / sizeof(ui_items[0]));
}
