#ifndef MENU_PAGES_H
#define MENU_PAGES_H

#include "lvgl.h"

// Menu page builders - each returns a screen with a list
lv_obj_t* menu_page_index_create(void);
lv_obj_t* menu_page_scenes_create(void);
lv_obj_t* menu_page_current_scene_create(void);
lv_obj_t* menu_page_scene_name_create(void);  // Scene -> Scene Name submenu
lv_obj_t* menu_page_device_config_create(void);
void menu_page_device_config_cleanup(void);  // Free PSRAM allocations after menu teardown
lv_obj_t* menu_page_touchwheel_create(void);
void menu_page_touchwheel_cleanup(void);     // Free PSRAM allocations for CC options
lv_obj_t* menu_page_pads_create(void);
  void menu_page_pads_cleanup(void);           // Free PSRAM allocations for CC options
lv_obj_t* menu_page_settings_create(void);
lv_obj_t* menu_page_about_create(void);

// Settings submenus
lv_obj_t* menu_page_config_create(void);
lv_obj_t* menu_page_touch_create(void);
lv_obj_t* menu_page_midi_create(void);
lv_obj_t* menu_page_settings_midi_control_create(void);
lv_obj_t* menu_page_expression_create(void);
void menu_page_expression_cleanup(void);      // Free PSRAM allocations for CC options
lv_obj_t* menu_page_settings_expression_create(void);  // Global expression settings
lv_obj_t* menu_page_cv_create(void);  // Settings -> CV (global settings)
lv_obj_t* menu_page_cv_scene_create(void);  // Scene -> CV
void menu_page_cv_scene_cleanup(void);      // Free PSRAM allocations for CC options
lv_obj_t* menu_page_sensor_create(void);
lv_obj_t* menu_page_proximity_scene_create(void);  // Scene -> Proximity
void menu_page_proximity_scene_cleanup(void);
lv_obj_t* menu_page_als_scene_create(void);        // Scene -> Ambient Light
void menu_page_als_scene_cleanup(void);
lv_obj_t* menu_page_lfo1_scene_create(void);       // Scene -> LFO1
void menu_page_lfo1_scene_cleanup(void);
lv_obj_t* menu_page_lfo2_scene_create(void);       // Scene -> LFO2
void menu_page_lfo2_scene_cleanup(void);
lv_obj_t* menu_page_rtg_scene_create(void);        // Scene -> RTG
lv_obj_t* menu_page_sample_hold_scene_create(void);   // Scene -> S+H
lv_obj_t* menu_page_cc_triggers_scene_create(void);   // Scene -> CC Triggers
lv_obj_t* menu_page_cc_trigger_slot_create(void);     // Scene -> CC Triggers -> slot
uint8_t cc_triggers_focus_slot_get(void);
void cc_triggers_focus_slot_set(uint8_t slot);
lv_obj_t* menu_page_note_track_scene_create(void); // Scene -> Note Track
void menu_page_note_track_scene_cleanup(void);
lv_obj_t* menu_page_cc_defaults_scene_create(void); // Scene -> CC Defaults
lv_obj_t* menu_page_settings_note_track_create(void); // Settings -> Note Track
lv_obj_t* menu_page_settings_scene_inspect_create(void); // Settings -> Scene Inspect
lv_obj_t* menu_page_tilt_scene_create(void);          // Scene -> Tilt (submenu)
lv_obj_t* menu_page_tilt_axis_scene_create(void);     // Scene -> Tilt -> X/Y (parameterized)
void menu_page_tilt_axis_scene_cleanup(void);         // Free PSRAM allocations for CC options
lv_obj_t* menu_page_settings_proximity_create(void);  // Settings -> Proximity
lv_obj_t* menu_page_settings_als_create(void);        // Settings -> Ambient Light
lv_obj_t* menu_page_settings_tilt_create(void);       // Settings -> Tilt
lv_obj_t* menu_page_buttons_scene_create(void);       // Scene -> Buttons
lv_obj_t* menu_page_on_load_scene_create(void);       // Scene -> On-Load
lv_obj_t* menu_page_on_play_scene_create(void);       // Scene -> On-Play
lv_obj_t* menu_page_tempo_create(void);
lv_obj_t* menu_page_led_create(void);
lv_obj_t* menu_page_buttons_create(void);
lv_obj_t* menu_page_bump_create(void);
lv_obj_t* menu_page_display_create(void);
lv_obj_t* menu_page_theme_create(void);

// Inspect Scene - scrollable scene summary (pad 10 open/close; pads 8 and 12 back)
lv_obj_t* menu_page_inspect_scene_create(void);
void menu_page_inspect_scene_cleanup(void);
bool inspect_scene_is_active(void);
void inspect_scene_invalidate_scroll(void);
void inspect_scene_rebind_input(void);
bool inspect_scene_jog_scroll(uint8_t pad_id);

#endif // MENU_PAGES_H
