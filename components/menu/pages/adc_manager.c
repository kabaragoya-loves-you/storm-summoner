#include "menu.h"
#include "menu_pages.h"
#include "adc_manager.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_ADC"

static void show_info(void* user_data) {
  (void)user_data;
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "ADC Manager\n"
    "Unit: ADC_UNIT_%d\n"
    "Bitwidth: %d-bit\n"
    "Attenuation: %ddB",
    1, 12, 12);
  
  menu_navigate_to_info("ADC Info", info_text);
}

static void action_sample_ref(void* user_data) {
  (void)user_data;
  // TODO: Implement sample reference action
  ESP_LOGI(TAG, "Sample reference - TODO: implement");
}

lv_obj_t* menu_page_adc_manager_create(void) {
  ESP_LOGI(TAG, "Creating ADC manager page");
  
  static menu_item_t adc_items[] = {
    { "Info", show_info, NULL, false },
    { "Sample Ref", action_sample_ref, NULL, false }
  };
  
  return menu_create_page("ADC", adc_items, 
    sizeof(adc_items) / sizeof(adc_items[0]));
}
