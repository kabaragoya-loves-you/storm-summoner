#include "menu.h"
#include "menu_pages.h"
#include "i2c_common.h"
#include "esp_log.h"

#define TAG "MENU_I2C"

static void action_scan(void* user_data) {
  (void)user_data;
  i2c_common_scan();
  ESP_LOGI(TAG, "I2C scan executed");
}

static void action_read(void* user_data) {
  (void)user_data;
  // TODO: Implement I2C read UI with address/register inputs
  ESP_LOGI(TAG, "I2C read - TODO: implement");
}

static void action_write(void* user_data) {
  (void)user_data;
  // TODO: Implement I2C write UI
  ESP_LOGI(TAG, "I2C write - TODO: implement");
}

static void toggle_debug(void* user_data) {
  (void)user_data;
  // TODO: Implement debug toggle
  ESP_LOGI(TAG, "Debug toggle - TODO: implement");
}

static void show_stats(void* user_data) {
  (void)user_data;
  // TODO: Implement stats display
  ESP_LOGI(TAG, "Stats - TODO: implement");
}

static void action_stats_reset(void* user_data) {
  (void)user_data;
  // TODO: Implement stats reset
  ESP_LOGI(TAG, "Stats reset - TODO: implement");
}

lv_obj_t* menu_page_i2c_common_create(void) {
  ESP_LOGI(TAG, "Creating I2C page");
  
  static menu_item_t i2c_items[] = {
    { "Scan", action_scan, NULL, false },
    { "Read", action_read, NULL, false },
    { "Write", action_write, NULL, false },
    { "Debug", toggle_debug, NULL, false },
    { "Stats", show_stats, NULL, false },
    { "Stats Reset", action_stats_reset, NULL, false }
  };
  
  return menu_create_action_page("I2C", i2c_items, 
    sizeof(i2c_items) / sizeof(i2c_items[0]));
}
