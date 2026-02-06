#include "menu.h"
#include "menu_pages.h"
#include "screensaver.h"
#include "esp_log.h"

#define TAG "MENU_SCREENSAVER"

static void action_enable(void* user_data) {
  (void)user_data;
  screensaver_enable();
  ESP_LOGI(TAG, "Screensaver enabled");
}

static void action_disable(void* user_data) {
  (void)user_data;
  screensaver_disable();
  ESP_LOGI(TAG, "Screensaver disabled");
}

static void set_mode_starfield(void* user_data) {
  (void)user_data;
  screensaver_set_mode(SCREENSAVER_MODE_STARFIELD);
  ESP_LOGI(TAG, "Screensaver mode set to: Starfield");
}

static void set_mode_elite(void* user_data) {
  (void)user_data;
  screensaver_set_mode(SCREENSAVER_MODE_ELITE);
  ESP_LOGI(TAG, "Screensaver mode set to: Elite");
}

static void set_mode_plasma(void* user_data) {
  (void)user_data;
  screensaver_set_mode(SCREENSAVER_MODE_PLASMA);
  ESP_LOGI(TAG, "Screensaver mode set to: Plasma");
}

static void set_delay(void* user_data) {
  (void)user_data;
  // TODO: Implement delay slider UI
  ESP_LOGI(TAG, "Set delay - TODO: implement slider");
}

lv_obj_t* menu_page_screensaver_create(void) {
  ESP_LOGI(TAG, "Creating screensaver page");
  
  static menu_item_t screensaver_items[] = {
    { "Enable", action_enable, NULL, false },
    { "Disable", action_disable, NULL, false },
    { "Mode: Starfield", set_mode_starfield, NULL, false },
    { "Mode: Elite", set_mode_elite, NULL, false },
    { "Mode: Plasma", set_mode_plasma, NULL, false },
    { "Set Delay", set_delay, NULL, false }
  };
  
  return menu_create_page("Screensaver", screensaver_items, 
    sizeof(screensaver_items) / sizeof(screensaver_items[0]));
}
