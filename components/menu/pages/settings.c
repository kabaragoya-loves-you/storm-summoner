#include "menu.h"
#include "menu_pages.h"
#include "esp_log.h"

#define TAG "MENU_SETTINGS"

// Navigation callbacks for Settings submenus
static void nav_to_config(void) {
  menu_navigate_to("Config", menu_page_config_create);
}

static void nav_to_touch(void) {
  menu_navigate_to("Touch", menu_page_touch_create);
}

static void nav_to_midi(void) {
  menu_navigate_to("MIDI", menu_page_midi_create);
}

static void nav_to_haptic(void) {
  menu_navigate_to("Haptic", menu_page_haptic_create);
}

static void nav_to_screensaver(void) {
  menu_navigate_to("Screensaver", menu_page_screensaver_create);
}

static void nav_to_dac(void) {
  menu_navigate_to("DAC", menu_page_dac_create);
}

static void nav_to_adc_manager(void) {
  menu_navigate_to("ADC", menu_page_adc_manager_create);
}

static void nav_to_expression(void) {
  menu_navigate_to("Expression", menu_page_expression_create);
}

static void nav_to_cv(void) {
  menu_navigate_to("CV", menu_page_cv_create);
}

static void nav_to_sensor(void) {
  menu_navigate_to("Sensor", menu_page_sensor_create);
}

static void nav_to_tempo(void) {
  menu_navigate_to("Tempo", menu_page_tempo_create);
}

static void nav_to_clock_sync(void) {
  menu_navigate_to("Clock Sync", menu_page_clock_sync_create);
}

static void nav_to_transport(void) {
  menu_navigate_to("Transport", menu_page_transport_create);
}

static void nav_to_input_manager(void) {
  menu_navigate_to("Input Mgr", menu_page_input_manager_create);
}

static void nav_to_led(void) {
  menu_navigate_to("LED", menu_page_led_create);
}

static void nav_to_buttons(void) {
  menu_navigate_to("Buttons", menu_page_buttons_create);
}

static void nav_to_bump(void) {
  menu_navigate_to("Bump", menu_page_bump_create);
}

static void nav_to_switch(void) {
  menu_navigate_to("Switch", menu_page_switch_create);
}

static void nav_to_i2c_common(void) {
  menu_navigate_to("I2C", menu_page_i2c_common_create);
}

static void nav_to_event_bus(void) {
  menu_navigate_to("Event Bus", menu_page_event_bus_create);
}

static void nav_to_app_settings(void) {
  menu_navigate_to("App Settings", menu_page_app_settings_create);
}

static void nav_to_ui(void) {
  menu_navigate_to("UI", menu_page_ui_create);
}

static void nav_to_display(void) {
  menu_navigate_to("Display", menu_page_display_create);
}

static void nav_to_usb_mode(void) {
  menu_navigate_to("USB Mode", menu_page_usb_mode_create);
}

static void nav_to_revision(void) {
  menu_navigate_to("Revision", menu_page_revision_create);
}

static void nav_to_assets_manager(void) {
  menu_navigate_to("Assets", menu_page_assets_manager_create);
}

static void nav_to_firmware_update(void) {
  menu_navigate_to("Firmware", menu_page_firmware_update_create);
}

lv_obj_t* menu_page_settings_create(void) {
  ESP_LOGI(TAG, "Creating settings page");
  
  static menu_item_t settings_items[] = {
    { "Config", nav_to_config, true },
    { "Touch", nav_to_touch, true },
    { "MIDI", nav_to_midi, true },
    { "Haptic", nav_to_haptic, true },
    { "Screensaver", nav_to_screensaver, true },
    { "DAC", nav_to_dac, true },
    { "ADC", nav_to_adc_manager, true },
    { "Expression", nav_to_expression, true },
    { "CV", nav_to_cv, true },
    { "Sensor", nav_to_sensor, true },
    { "Tempo", nav_to_tempo, true },
    { "Clock Sync", nav_to_clock_sync, true },
    { "Transport", nav_to_transport, true },
    { "Input Mgr", nav_to_input_manager, true },
    { "LED", nav_to_led, true },
    { "Buttons", nav_to_buttons, true },
    { "Bump", nav_to_bump, true },
    { "Switch", nav_to_switch, true },
    { "I2C", nav_to_i2c_common, true },
    { "Event Bus", nav_to_event_bus, true },
    { "App Settings", nav_to_app_settings, true },
    { "UI", nav_to_ui, true },
    { "Display", nav_to_display, true },
    { "USB Mode", nav_to_usb_mode, true },
    { "Revision", nav_to_revision, true },
    { "Assets", nav_to_assets_manager, true },
    { "Firmware", nav_to_firmware_update, true }
  };
  
  return menu_create_page("Settings", settings_items, 
    sizeof(settings_items) / sizeof(settings_items[0]));
}
