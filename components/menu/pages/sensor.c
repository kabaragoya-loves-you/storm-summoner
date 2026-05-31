#include "menu.h"
#include "menu_pages.h"
#include "sensor.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_SENSOR"

static void show_info(void* user_data) {
  (void)user_data;
  uint16_t ps_raw = get_ps();
  uint16_t als_raw = get_als();
  uint16_t ps_min, ps_max, als_min, als_max;
  
  proximity_get_calibration(&ps_min, &ps_max);
  als_get_calibration(&als_min, &als_max);
  
  char info_text[512];
  snprintf(info_text, sizeof(info_text),
    "SENSOR\n"
    "Proximity:\n"
    "  Raw: %u (range: %u-%u)\n"
    "  Deadzone: %u\n"
    "Ambient Light:\n"
    "  Raw: %u (range: %u-%u)\n"
    "  Deadzone: %u",
    (unsigned)ps_raw, (unsigned)ps_min, (unsigned)ps_max,
    (unsigned)proximity_get_deadzone(),
    (unsigned)als_raw, (unsigned)als_min, (unsigned)als_max,
    (unsigned)als_get_deadzone());
  
  menu_navigate_to_info("Sensor Info", info_text);
}

static void action_calibrate_ps(void* user_data) {
  (void)user_data;
  // TODO: Implement calibration UI with progress bar
  ESP_LOGI(TAG, "Calibrate PS - TODO: implement");
}

static void action_calibrate_als(void* user_data) {
  (void)user_data;
  // TODO: Implement calibration UI with progress bar
  ESP_LOGI(TAG, "Calibrate ALS - TODO: implement");
}

lv_obj_t* menu_page_sensor_create(void) {
  ESP_LOGI(TAG, "Creating sensor page");
  
  static menu_item_t sensor_items[] = {
    { "Info", show_info, NULL, false, MENU_ITEM_KIND_ACTION },
    { "Calibrate PS", action_calibrate_ps, NULL, false, MENU_ITEM_KIND_ACTION },
    { "Calibrate ALS", action_calibrate_als, NULL, false, MENU_ITEM_KIND_ACTION }
  };
  
  return menu_create_page("Sensor", sensor_items, 
    sizeof(sensor_items) / sizeof(sensor_items[0]));
}
