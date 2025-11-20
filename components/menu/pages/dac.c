#include "menu.h"
#include "menu_pages.h"
#include "dac.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_DAC"

static void show_info(void) {
  uint16_t value = 0;
  mcp4725_cv_range_t range;
  
  dac_get_value(&value);
  dac_get_cv_range(&range);
  
  const char* range_str;
  switch (range) {
    case MCP4725_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case MCP4725_RANGE_10V: range_str = "0-10V"; break;
    case MCP4725_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case MCP4725_RANGE_5V: range_str = "0-5V"; break;
    case MCP4725_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  float vref = dac_get_vref();
  float voltage = dac_value_to_voltage(value, vref);
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "DAC\n"
    "Value: %u (0-4095)\n"
    "Voltage: %.3f V\n"
    "Range: %s\n"
    "VREF: %.3f V",
    (unsigned)value, voltage, range_str, vref);
  
  menu_navigate_to_info("DAC Info", info_text);
}

static void action_set(void) {
  // TODO: Implement slider UI for DAC value
  ESP_LOGI(TAG, "Set DAC value - TODO: implement slider");
}

static void action_readback(void) {
  dac_debug_readback();
  ESP_LOGI(TAG, "DAC readback executed");
}

lv_obj_t* menu_page_dac_create(void) {
  ESP_LOGI(TAG, "Creating DAC page");
  
  static menu_item_t dac_items[] = {
    { "Info", show_info, false },
    { "Set Value", action_set, false },
    { "Readback", action_readback, false }
  };
  
  return menu_create_page("DAC", dac_items, 
    sizeof(dac_items) / sizeof(dac_items[0]));
}

