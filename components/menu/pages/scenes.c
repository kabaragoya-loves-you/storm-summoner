#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_SCENES"

// TODO: Add scene management items here

// Placeholder submenu items
static const menu_item_t scenes_items[] = {
  { "Coming Soon", NULL, false }
};

lv_obj_t* menu_page_scenes_create(void) {
  ESP_LOGI(TAG, "Creating scenes page");
  return menu_create_page("Scenes", scenes_items, 
    sizeof(scenes_items) / sizeof(scenes_items[0]));
}

