#include "menu.h"
#include "menu_pages.h"
#include "haptic.h"
#include "esp_log.h"

#define TAG "MENU_HAPTIC"

static void set_mode_0(void) { haptic_set_mode(0); ESP_LOGI(TAG, "Haptic mode set to 0"); }
static void set_mode_1(void) { haptic_set_mode(1); ESP_LOGI(TAG, "Haptic mode set to 1"); }
static void set_mode_2(void) { haptic_set_mode(2); ESP_LOGI(TAG, "Haptic mode set to 2"); }
static void set_mode_3(void) { haptic_set_mode(3); ESP_LOGI(TAG, "Haptic mode set to 3"); }
static void set_mode_4(void) { haptic_set_mode(4); ESP_LOGI(TAG, "Haptic mode set to 4"); }
static void set_mode_5(void) { haptic_set_mode(5); ESP_LOGI(TAG, "Haptic mode set to 5"); }
static void set_mode_6(void) { haptic_set_mode(6); ESP_LOGI(TAG, "Haptic mode set to 6"); }
static void set_mode_7(void) { haptic_set_mode(7); ESP_LOGI(TAG, "Haptic mode set to 7"); }

static void action_go(void) {
  haptic_go();
  ESP_LOGI(TAG, "Haptic triggered");
}

static void action_stop(void) {
  haptic_stop();
  ESP_LOGI(TAG, "Haptic stopped");
}

lv_obj_t* menu_page_haptic_create(void) {
  ESP_LOGI(TAG, "Creating haptic page");
  
  static menu_item_t haptic_items[] = {
    { "Mode: 0", set_mode_0, false },
    { "Mode: 1", set_mode_1, false },
    { "Mode: 2", set_mode_2, false },
    { "Mode: 3", set_mode_3, false },
    { "Mode: 4", set_mode_4, false },
    { "Mode: 5", set_mode_5, false },
    { "Mode: 6", set_mode_6, false },
    { "Mode: 7", set_mode_7, false },
    { "Go", action_go, false },
    { "Stop", action_stop, false }
  };
  
  return menu_create_page("Haptic", haptic_items, 
    sizeof(haptic_items) / sizeof(haptic_items[0]));
}

