#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_DEVICE_CONFIG"

// TODO: Add device configuration items here

// Placeholder submenu items
static const menu_item_t device_config_items[] = {
  { "Coming Soon", NULL, false }
};

lv_obj_t* menu_page_device_config_create(void) {
  ESP_LOGI(TAG, "Creating device config page");
  return menu_create_page("Device Config", device_config_items, 
    sizeof(device_config_items) / sizeof(device_config_items[0]));
}

