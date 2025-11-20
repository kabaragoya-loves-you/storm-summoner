#include "menu.h"
#include "menu_pages.h"
#include "event_bus.h"
#include "esp_log.h"

#define TAG "MENU_EVENT_BUS"

static void show_stats(void) {
  const char* info_text = "EVENT BUS STATS\nStatistics available";
  menu_navigate_to_info("Event Bus Stats", info_text);
}

static void action_profile_start(void) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_start();
  ESP_LOGI(TAG, "Profiling started");
  #else
  ESP_LOGI(TAG, "Profiling not enabled");
  #endif
}

static void action_profile_stop(void) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_stop();
  ESP_LOGI(TAG, "Profiling stopped");
  #else
  ESP_LOGI(TAG, "Profiling not enabled");
  #endif
}

static void action_profile_report(void) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_report();
  ESP_LOGI(TAG, "Profiling report displayed");
  #else
  ESP_LOGI(TAG, "Profiling not enabled");
  #endif
}

static void action_profile_reset(void) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_reset();
  ESP_LOGI(TAG, "Profiling reset");
  #else
  ESP_LOGI(TAG, "Profiling not enabled");
  #endif
}

lv_obj_t* menu_page_event_bus_create(void) {
  ESP_LOGI(TAG, "Creating event bus page");
  
  static menu_item_t event_bus_items[] = {
    { "Stats", show_stats, false },
    { "Profile Start", action_profile_start, false },
    { "Profile Stop", action_profile_stop, false },
    { "Profile Report", action_profile_report, false },
    { "Profile Reset", action_profile_reset, false }
  };
  
  return menu_create_action_page("Event Bus", event_bus_items, 
    sizeof(event_bus_items) / sizeof(event_bus_items[0]));
}

