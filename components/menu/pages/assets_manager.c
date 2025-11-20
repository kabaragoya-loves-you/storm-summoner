#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_ASSETS"

static void show_info(void) {
  const char* info_text = "ASSETS MANAGER\nAssets manager initialized";
  menu_navigate_to_info("Assets Info", info_text);
}

lv_obj_t* menu_page_assets_manager_create(void) {
  ESP_LOGI(TAG, "Creating assets manager page");
  
  static menu_item_t assets_items[] = {
    { "Info", show_info, false }
  };
  
  return menu_create_page("Assets", assets_items, 
    sizeof(assets_items) / sizeof(assets_items[0]));
}

