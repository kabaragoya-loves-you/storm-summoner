#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_SETTINGS"

// TODO: Add settings items here

// Placeholder submenu items
static const menu_item_t settings_items[] = {
  { "Coming Soon", NULL, false }
};

lv_obj_t* menu_page_settings_create(void) {
  ESP_LOGI(TAG, "Creating settings page");
  return menu_create_page("Settings", settings_items, 
    sizeof(settings_items) / sizeof(settings_items[0]));
}

