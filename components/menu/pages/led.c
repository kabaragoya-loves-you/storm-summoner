#include "menu.h"
#include "menu_pages.h"
#include "tempo.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "MENU_LED"

static void show_info(void) {
  bool enabled = led_get_enabled();
  led_mode_t mode = led_get_mode();
  bool sundial = led_get_sundial_mode();
  
  char info_text[256];
  snprintf(info_text, sizeof(info_text),
    "LED STATUS\n"
    "Enabled: %s\n"
    "Mode: %s\n"
    "Sundial mode: %s",
    enabled ? "yes" : "no",
    mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime",
    sundial ? "yes" : "no");
  
  menu_navigate_to_info("LED Info", info_text);
}

static void action_on(void) { led_set_on(); ESP_LOGI(TAG, "LED on"); }
static void action_off(void) { led_set_off(); ESP_LOGI(TAG, "LED off"); }
static void action_flash(void) {
  flash_led(100); // Default 100ms
  ESP_LOGI(TAG, "LED flashed");
}
static void toggle_enable(void) {
  bool enabled = led_get_enabled();
  led_set_enabled(!enabled);
  ESP_LOGI(TAG, "LED %s", !enabled ? "enabled" : "disabled");
}
static void set_mode_daylight(void) { led_set_mode(LED_MODE_DAYLIGHT); ESP_LOGI(TAG, "Mode: Daylight"); }
static void set_mode_nighttime(void) { led_set_mode(LED_MODE_NIGHTTIME); ESP_LOGI(TAG, "Mode: Nighttime"); }
static void toggle_sundial(void) {
  bool sundial = led_get_sundial_mode();
  led_set_sundial_mode(!sundial);
  ESP_LOGI(TAG, "Sundial mode: %s", !sundial ? "on" : "off");
}

lv_obj_t* menu_page_led_create(void) {
  ESP_LOGI(TAG, "Creating LED page");
  
  static menu_item_t led_items[] = {
    { "Info", show_info, false },
    { "On", action_on, false },
    { "Off", action_off, false },
    { "Flash", action_flash, false },
    { "Enable", toggle_enable, false },
    { "Mode: Daylight", set_mode_daylight, false },
    { "Mode: Nighttime", set_mode_nighttime, false },
    { "Sundial", toggle_sundial, false }
  };
  
  return menu_create_page("LED", led_items, 
    sizeof(led_items) / sizeof(led_items[0]));
}
