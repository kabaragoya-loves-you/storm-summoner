#include "menu.h"
#include "menu_pages.h"
#include "app_settings.h"
#include "esp_log.h"

#define TAG "MENU_APP_SETTINGS"

static void action_list(void) {
  // TODO: Implement NVS key list display
  ESP_LOGI(TAG, "List keys - TODO: implement");
}

static void action_get(void) {
  // TODO: Implement get value UI
  ESP_LOGI(TAG, "Get value - TODO: implement");
}

static void action_set_u8(void) {
  // TODO: Implement set u8 UI
  ESP_LOGI(TAG, "Set u8 - TODO: implement");
}

static void action_set_u16(void) {
  // TODO: Implement set u16 UI
  ESP_LOGI(TAG, "Set u16 - TODO: implement");
}

static void action_set_u32(void) {
  // TODO: Implement set u32 UI
  ESP_LOGI(TAG, "Set u32 - TODO: implement");
}

static void action_set_bool(void) {
  // TODO: Implement set bool UI
  ESP_LOGI(TAG, "Set bool - TODO: implement");
}

static void action_set_str(void) {
  // TODO: Implement set string UI
  ESP_LOGI(TAG, "Set string - TODO: implement");
}

static void action_erase(void) {
  // TODO: Implement erase key UI
  ESP_LOGI(TAG, "Erase key - TODO: implement");
}

static void action_erase_all(void) {
  // TODO: Implement erase all confirmation
  ESP_LOGI(TAG, "Erase all - TODO: implement");
}

lv_obj_t* menu_page_app_settings_create(void) {
  ESP_LOGI(TAG, "Creating app settings page");
  
  static menu_item_t app_settings_items[] = {
    { "List Keys", action_list, false },
    { "Get Value", action_get, false },
    { "Set u8", action_set_u8, false },
    { "Set u16", action_set_u16, false },
    { "Set u32", action_set_u32, false },
    { "Set Bool", action_set_bool, false },
    { "Set String", action_set_str, false },
    { "Erase Key", action_erase, false },
    { "Erase All", action_erase_all, false }
  };
  
  return menu_create_page("App Settings", app_settings_items, 
    sizeof(app_settings_items) / sizeof(app_settings_items[0]));
}

