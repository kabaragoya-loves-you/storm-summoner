#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_DISPLAY"

static void show_info(void) {
  const char* info_text = "DISPLAY\nDisplay initialized";
  menu_navigate_to_info("Display Info", info_text);
}

lv_obj_t* menu_page_display_create(void) {
  ESP_LOGI(TAG, "Creating display page");
  
  static menu_item_t display_items[] = {
    { "Info", show_info, false }
  };
  
  return menu_create_page("Display", display_items, 
    sizeof(display_items) / sizeof(display_items[0]));
}

