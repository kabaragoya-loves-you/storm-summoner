#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

#define TAG "MENU_SETTINGS"

// Navigation callbacks for Settings submenus
static void nav_to_config(void* user_data) {
  (void)user_data;
  menu_navigate_to("Config", menu_page_config_create);
}

static void nav_to_touch(void* user_data) {
  (void)user_data;
  menu_navigate_to("Touch", menu_page_touch_create);
}

static void nav_to_midi(void* user_data) {
  (void)user_data;
  menu_navigate_to("MIDI", menu_page_midi_create);
}

static void nav_to_haptic(void* user_data) {
  (void)user_data;
  menu_navigate_to("Haptic", menu_page_haptic_create);
}

static void nav_to_screensaver(void* user_data) {
  (void)user_data;
  menu_navigate_to("Screensaver", menu_page_screensaver_create);
}

static void nav_to_dac(void* user_data) {
  (void)user_data;
  menu_navigate_to("DAC", menu_page_dac_create);
}

static void nav_to_adc_manager(void* user_data) {
  (void)user_data;
  menu_navigate_to("ADC", menu_page_adc_manager_create);
}

static void nav_to_expression(void* user_data) {
  (void)user_data;
  menu_navigate_to("Expression", menu_page_settings_expression_create);
}

static void nav_to_cv(void* user_data) {
  (void)user_data;
  menu_navigate_to("Control Voltage", menu_page_cv_create);
}

static void nav_to_proximity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Proximity", menu_page_settings_proximity_create);
}

static void nav_to_ambient_light(void* user_data) {
  (void)user_data;
  menu_navigate_to("Ambient Light", menu_page_settings_als_create);
}

static void nav_to_tempo(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tempo", menu_page_tempo_create);
}

static void nav_to_clock_sync(void* user_data) {
  (void)user_data;
  menu_navigate_to("Clock Sync", menu_page_clock_sync_create);
}

static void nav_to_transport(void* user_data) {
  (void)user_data;
  menu_navigate_to("Transport", menu_page_transport_create);
}

static void nav_to_input_manager(void* user_data) {
  (void)user_data;
  menu_navigate_to("Input Mgr", menu_page_input_manager_create);
}

static void nav_to_led(void* user_data) {
  (void)user_data;
  menu_navigate_to("LED", menu_page_led_create);
}

static void nav_to_buttons(void* user_data) {
  (void)user_data;
  menu_navigate_to("Buttons", menu_page_buttons_create);
}

static void nav_to_bump(void* user_data) {
  (void)user_data;
  menu_navigate_to("Bump", menu_page_bump_create);
}

static void nav_to_switch(void* user_data) {
  (void)user_data;
  menu_navigate_to("Switch", menu_page_switch_create);
}

static void nav_to_i2c_common(void* user_data) {
  (void)user_data;
  menu_navigate_to("I2C", menu_page_i2c_common_create);
}

static void nav_to_event_bus(void* user_data) {
  (void)user_data;
  menu_navigate_to("Event Bus", menu_page_event_bus_create);
}

static void nav_to_app_settings(void* user_data) {
  (void)user_data;
  menu_navigate_to("App Settings", menu_page_app_settings_create);
}

static void nav_to_ui(void* user_data) {
  (void)user_data;
  menu_navigate_to("UI", menu_page_ui_create);
}

static void nav_to_display(void* user_data) {
  (void)user_data;
  menu_navigate_to("Display", menu_page_display_create);
}

static void nav_to_usb_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("USB Mode", menu_page_usb_mode_create);
}

static void nav_to_revision(void* user_data) {
  (void)user_data;
  menu_navigate_to("Revision", menu_page_revision_create);
}

static void nav_to_assets_manager(void* user_data) {
  (void)user_data;
  menu_navigate_to("Assets", menu_page_assets_manager_create);
}

static void nav_to_firmware_update(void* user_data) {
  (void)user_data;
  menu_navigate_to("Firmware", menu_page_firmware_update_create);
}

// Factory Reset confirmation
static void factory_reset_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (selected_index == 1) {  // "Reset" selected
    ESP_LOGW(TAG, "Factory reset confirmed - erasing NVS and restarting");
    nvs_flash_erase();
    esp_restart();
  }
  // selected_index == 0 means "Cancel" - just go back
  menu_navigate_back();
}

static lv_obj_t* factory_reset_roller_create(void) {
  return menu_create_roller_page("Factory Reset?", "Cancel\nReset", 0,
    factory_reset_confirm_cb, NULL);
}

static void nav_to_factory_reset(void* user_data) {
  (void)user_data;
  menu_navigate_to("Factory Reset", factory_reset_roller_create);
}

lv_obj_t* menu_page_settings_create(void) {
  ESP_LOGI(TAG, "Creating settings page");
  
  static menu_item_t settings_items[] = {
    { "Config", nav_to_config, NULL, true },
    { "Touch", nav_to_touch, NULL, true },
    { "MIDI", nav_to_midi, NULL, true },
    { "Haptic", nav_to_haptic, NULL, true },
    { "Screensaver", nav_to_screensaver, NULL, true },
    { "DAC", nav_to_dac, NULL, true },
    { "ADC", nav_to_adc_manager, NULL, true },
    { "Expression", nav_to_expression, NULL, true },
    { "Control Voltage", nav_to_cv, NULL, true },
    { "Proximity", nav_to_proximity, NULL, true },
    { "Ambient Light", nav_to_ambient_light, NULL, true },
    { "Tempo", nav_to_tempo, NULL, true },
    { "Clock Sync", nav_to_clock_sync, NULL, true },
    { "Transport", nav_to_transport, NULL, true },
    { "Input Mgr", nav_to_input_manager, NULL, true },
    { "LED", nav_to_led, NULL, true },
    { "Buttons", nav_to_buttons, NULL, true },
    { "Bump", nav_to_bump, NULL, true },
    { "Switch", nav_to_switch, NULL, true },
    { "I2C", nav_to_i2c_common, NULL, true },
    { "Event Bus", nav_to_event_bus, NULL, true },
    { "App Settings", nav_to_app_settings, NULL, true },
    { "UI", nav_to_ui, NULL, true },
    { "Display", nav_to_display, NULL, true },
    { "USB Mode", nav_to_usb_mode, NULL, true },
    { "Revision", nav_to_revision, NULL, true },
    { "Assets", nav_to_assets_manager, NULL, true },
    { "Firmware", nav_to_firmware_update, NULL, true },
    { "Factory Reset", nav_to_factory_reset, NULL, true }
  };
  
  return menu_create_page("Settings", settings_items, 
    sizeof(settings_items) / sizeof(settings_items[0]));
}
