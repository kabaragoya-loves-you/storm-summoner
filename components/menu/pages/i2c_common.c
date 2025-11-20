#include "menu.h"
#include "menu_pages.h"
#include "i2c_common.h"
#include "esp_log.h"

#define TAG "MENU_I2C"

static void action_scan(void) {
  i2c_common_scan();
  ESP_LOGI(TAG, "I2C scan executed");
}

static void action_read(void) {
  // TODO: Implement I2C read UI with address/register inputs
  ESP_LOGI(TAG, "I2C read - TODO: implement");
}

static void action_write(void) {
  // TODO: Implement I2C write UI
  ESP_LOGI(TAG, "I2C write - TODO: implement");
}

static void toggle_debug(void) {
  // TODO: Implement debug toggle
  ESP_LOGI(TAG, "Debug toggle - TODO: implement");
}

static void show_stats(void) {
  // TODO: Implement stats display
  ESP_LOGI(TAG, "Stats - TODO: implement");
}

static void action_stats_reset(void) {
  // TODO: Implement stats reset
  ESP_LOGI(TAG, "Stats reset - TODO: implement");
}

lv_obj_t* menu_page_i2c_common_create(void) {
  ESP_LOGI(TAG, "Creating I2C page");
  
  static menu_item_t i2c_items[] = {
    { "Scan", action_scan, false },
    { "Read", action_read, false },
    { "Write", action_write, false },
    { "Debug", toggle_debug, false },
    { "Stats", show_stats, false },
    { "Stats Reset", action_stats_reset, false }
  };
  
  return menu_create_action_page("I2C", i2c_items, 
    sizeof(i2c_items) / sizeof(i2c_items[0]));
}

