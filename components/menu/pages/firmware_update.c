#include "menu.h"
#include "menu_pages.h"
#include "firmware_update.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_FW_UPDATE"

static void show_info(void) {
  firmware_update_state_t fw_state = firmware_update_get_state();
  assets_update_state_t assets_state = assets_update_get_state();
  uint8_t fw_progress = firmware_update_get_progress();
  uint8_t assets_progress = assets_update_get_progress();
  
  const char* fw_str;
  switch (fw_state) {
    case FIRMWARE_UPDATE_IDLE: fw_str = "Idle"; break;
    case FIRMWARE_UPDATE_IN_PROGRESS: fw_str = "In Progress"; break;
    case FIRMWARE_UPDATE_COMPLETE: fw_str = "Complete"; break;
    case FIRMWARE_UPDATE_ERROR: fw_str = "Error"; break;
    default: fw_str = "Unknown"; break;
  }
  
  const char* assets_str;
  switch (assets_state) {
    case ASSETS_UPDATE_IDLE: assets_str = "Idle"; break;
    case ASSETS_UPDATE_IN_PROGRESS: assets_str = "In Progress"; break;
    case ASSETS_UPDATE_COMPLETE: assets_str = "Complete"; break;
    case ASSETS_UPDATE_ERROR: assets_str = "Error"; break;
    default: assets_str = "Unknown"; break;
  }
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "FIRMWARE UPDATE\n"
    "Firmware state: %s\n"
    "Firmware progress: %u%%\n"
    "Assets state: %s\n"
    "Assets progress: %u%%",
    fw_str, (unsigned)fw_progress, assets_str, (unsigned)assets_progress);
  
  menu_navigate_to_info("Firmware Info", info_text);
}

lv_obj_t* menu_page_firmware_update_create(void) {
  ESP_LOGI(TAG, "Creating firmware update page");
  
  static menu_item_t fw_items[] = {
    { "Info", show_info, false }
  };
  
  return menu_create_page("Firmware", fw_items, 
    sizeof(fw_items) / sizeof(fw_items[0]));
}

