#include "menu.h"
#include "menu_pages.h"
#include "touch.h"
#include "esp_log.h"

#define TAG "MENU_TOUCH"

static void action_calibrate(void* user_data) {
  (void)user_data;
  ESP_LOGI(TAG, "Starting touch calibration...");
  force_touch_calibration();
  ESP_LOGI(TAG, "Calibration complete");
}

static void action_reset(void* user_data) {
  (void)user_data;
  ESP_LOGI(TAG, "Resetting stuck touch pads...");
  touch_reset_stuck_pads();
  ESP_LOGI(TAG, "Touch pads reset");
}

static void action_debug(void* user_data) {
  (void)user_data;
  ESP_LOGI(TAG, "Enabling touch debug logging");
  touch_enable_debug_logging();
}

static void action_query(void* user_data) {
  (void)user_data;
  // TODO: Implement pad selection UI
  ESP_LOGI(TAG, "Query pad - TODO: implement pad selection");
}

static void action_endless(void* user_data) {
  (void)user_data;
  // TODO: Implement touchwheel test UI
  ESP_LOGI(TAG, "Endless test - TODO: implement test UI");
}

static void action_odometer(void* user_data) {
  (void)user_data;
  // TODO: Implement touchwheel test UI
  ESP_LOGI(TAG, "Odometer test - TODO: implement test UI");
}

static void action_bipolar(void* user_data) {
  (void)user_data;
  // TODO: Implement touchwheel test UI
  ESP_LOGI(TAG, "Bipolar test - TODO: implement test UI");
}

lv_obj_t* menu_page_touch_create(void) {
  ESP_LOGI(TAG, "Creating touch page");
  
  static menu_item_t touch_items[] = {
    { "Calibrate", action_calibrate, NULL, false },
    { "Reset", action_reset, NULL, false },
    { "Debug", action_debug, NULL, false },
    { "Query", action_query, NULL, false },
    { "Endless Test", action_endless, NULL, false },
    { "Odometer Test", action_odometer, NULL, false },
    { "Bipolar Test", action_bipolar, NULL, false }
  };
  
  return menu_create_action_page("Touch", touch_items, 
    sizeof(touch_items) / sizeof(touch_items[0]));
}
