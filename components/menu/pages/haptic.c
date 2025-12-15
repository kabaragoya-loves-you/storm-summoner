#include "menu.h"
#include "menu_pages.h"
#include "haptic.h"
#include "esp_log.h"

#define TAG "MENU_HAPTIC"

static void set_mode_0(void* user_data) { (void)user_data; haptic_set_mode(0); ESP_LOGI(TAG, "Haptic mode set to 0"); }
static void set_mode_1(void* user_data) { (void)user_data; haptic_set_mode(1); ESP_LOGI(TAG, "Haptic mode set to 1"); }
static void set_mode_2(void* user_data) { (void)user_data; haptic_set_mode(2); ESP_LOGI(TAG, "Haptic mode set to 2"); }
static void set_mode_3(void* user_data) { (void)user_data; haptic_set_mode(3); ESP_LOGI(TAG, "Haptic mode set to 3"); }
static void set_mode_4(void* user_data) { (void)user_data; haptic_set_mode(4); ESP_LOGI(TAG, "Haptic mode set to 4"); }
static void set_mode_5(void* user_data) { (void)user_data; haptic_set_mode(5); ESP_LOGI(TAG, "Haptic mode set to 5"); }
static void set_mode_6(void* user_data) { (void)user_data; haptic_set_mode(6); ESP_LOGI(TAG, "Haptic mode set to 6"); }
static void set_mode_7(void* user_data) { (void)user_data; haptic_set_mode(7); ESP_LOGI(TAG, "Haptic mode set to 7"); }

static void action_go(void* user_data) {
  (void)user_data;
  haptic_go();
  ESP_LOGI(TAG, "Haptic triggered");
}

static void action_stop(void* user_data) {
  (void)user_data;
  haptic_stop();
  ESP_LOGI(TAG, "Haptic stopped");
}

lv_obj_t* menu_page_haptic_create(void) {
  ESP_LOGI(TAG, "Creating haptic page");
  
  static menu_item_t haptic_items[] = {
    { "Mode: 0", set_mode_0, NULL, false },
    { "Mode: 1", set_mode_1, NULL, false },
    { "Mode: 2", set_mode_2, NULL, false },
    { "Mode: 3", set_mode_3, NULL, false },
    { "Mode: 4", set_mode_4, NULL, false },
    { "Mode: 5", set_mode_5, NULL, false },
    { "Mode: 6", set_mode_6, NULL, false },
    { "Mode: 7", set_mode_7, NULL, false },
    { "Go", action_go, NULL, false },
    { "Stop", action_stop, NULL, false }
  };
  
  return menu_create_page("Haptic", haptic_items, 
    sizeof(haptic_items) / sizeof(haptic_items[0]));
}
