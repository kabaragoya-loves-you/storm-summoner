#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_ABOUT"

// TODO: Add about/info items here

// Placeholder submenu items
static const menu_item_t about_items[] = {
  { "Coming Soon", NULL, false }
};

lv_obj_t* menu_page_about_create(void) {
  ESP_LOGI(TAG, "Creating about page");
  return menu_create_page("About", about_items, 
    sizeof(about_items) / sizeof(about_items[0]));
}

