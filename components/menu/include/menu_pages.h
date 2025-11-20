#ifndef MENU_PAGES_H
#define MENU_PAGES_H

#include "lvgl.h"

// Menu page builders - each returns a screen with a list
lv_obj_t* menu_page_index_create(void);
lv_obj_t* menu_page_scenes_create(void);
lv_obj_t* menu_page_device_config_create(void);
lv_obj_t* menu_page_settings_create(void);
lv_obj_t* menu_page_about_create(void);

#endif // MENU_PAGES_H

