#ifndef MENU_PAGES_H
#define MENU_PAGES_H

#include "lvgl.h"

// Menu page builders - each returns a screen with a list
lv_obj_t* menu_page_index_create(void);
lv_obj_t* menu_page_scenes_create(void);
lv_obj_t* menu_page_current_scene_create(void);
lv_obj_t* menu_page_device_config_create(void);
void menu_page_device_config_cleanup(void);  // Free PSRAM allocations after menu teardown
lv_obj_t* menu_page_settings_create(void);
lv_obj_t* menu_page_about_create(void);

// Settings submenus
lv_obj_t* menu_page_config_create(void);
lv_obj_t* menu_page_touch_create(void);
lv_obj_t* menu_page_midi_create(void);
lv_obj_t* menu_page_haptic_create(void);
lv_obj_t* menu_page_screensaver_create(void);
lv_obj_t* menu_page_dac_create(void);
lv_obj_t* menu_page_adc_manager_create(void);
lv_obj_t* menu_page_expression_create(void);
lv_obj_t* menu_page_cv_create(void);
lv_obj_t* menu_page_sensor_create(void);
lv_obj_t* menu_page_tempo_create(void);
lv_obj_t* menu_page_clock_sync_create(void);
lv_obj_t* menu_page_transport_create(void);
lv_obj_t* menu_page_input_manager_create(void);
lv_obj_t* menu_page_led_create(void);
lv_obj_t* menu_page_buttons_create(void);
lv_obj_t* menu_page_bump_create(void);
lv_obj_t* menu_page_switch_create(void);
lv_obj_t* menu_page_i2c_common_create(void);
lv_obj_t* menu_page_event_bus_create(void);
lv_obj_t* menu_page_app_settings_create(void);
lv_obj_t* menu_page_ui_create(void);
lv_obj_t* menu_page_display_create(void);
lv_obj_t* menu_page_usb_mode_create(void);
lv_obj_t* menu_page_revision_create(void);
lv_obj_t* menu_page_assets_manager_create(void);
lv_obj_t* menu_page_firmware_update_create(void);

#endif // MENU_PAGES_H
