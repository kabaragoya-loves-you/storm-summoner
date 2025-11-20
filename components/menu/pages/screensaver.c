#include "menu.h"
#include "menu_pages.h"
#include "screensaver.h"
#include "esp_log.h"

#define TAG "MENU_SCREENSAVER"

static void action_enable(void) {
  screensaver_enable();
  ESP_LOGI(TAG, "Screensaver enabled");
}

static void action_disable(void) {
  screensaver_disable();
  ESP_LOGI(TAG, "Screensaver disabled");
}

static void set_mode_starfield(void) {
  screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);
  ESP_LOGI(TAG, "Screensaver mode set to: Starfield");
}

static void set_mode_elite(void) {
  screensaver_set_mode(SCREENSAVER_MODE_ELITE);
  ESP_LOGI(TAG, "Screensaver mode set to: Elite");
}

static void set_delay(void) {
  // TODO: Implement delay slider UI
  ESP_LOGI(TAG, "Set delay - TODO: implement slider");
}

lv_obj_t* menu_page_screensaver_create(void) {
  ESP_LOGI(TAG, "Creating screensaver page");
  
  static menu_item_t screensaver_items[] = {
    { "Enable", action_enable, false },
    { "Disable", action_disable, false },
    { "Mode: Starfield", set_mode_starfield, false },
    { "Mode: Elite", set_mode_elite, false },
    { "Set Delay", set_delay, false }
  };
  
  return menu_create_page("Screensaver", screensaver_items, 
    sizeof(screensaver_items) / sizeof(screensaver_items[0]));
}

