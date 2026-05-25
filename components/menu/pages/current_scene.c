#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "scene_name_gen.h"
#include "name_edit.h"
#include "ui.h"
#include "tempo.h"
#include "assets_types.h"
#include "assets_manager.h"
#include "device_config.h"
#include "midi_out.h"
#include "config.h"
#include "display_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CURRENT_SCENE"

// Static storage for menu items and labels
#define MAX_SCENE_ITEMS 30
static menu_item_t s_scene_items[MAX_SCENE_ITEMS];
static char s_page_title[24];
static char s_preset_label[32];
static char s_send_pc_label[24];
static char s_bpm_label[24];
static char s_screen_label[32];
static char s_time_sig_label[16];
static char s_divider_label[24];
static char s_clock_label[24];
static char s_transport_label[24];
static char s_send_clock_label[24];
static char s_pedal_label[80];
static char s_midi_channel_label[32];
static char s_note_channel_label[32];
static char s_trs_type_label[32];

// Dynamic menu for vendor/pedal selection (allocated in PSRAM)
typedef struct {
  menu_item_t* items;
  char (*labels)[64];
  uint32_t* indices;
  uint32_t count;
  uint32_t capacity;
} scene_dynamic_menu_t;

static scene_dynamic_menu_t s_scene_vendor_menu = {0};
static scene_dynamic_menu_t s_scene_pedal_menu = {0};
static char s_scene_selected_vendor[64];
static char s_scene_pedal_title[80];
static char s_pending_pedal_slug[64];  // Slug selected, pending confirmation

// Navigation callbacks for assignment submenus
static void nav_to_touchwheel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Touchwheel", menu_page_touchwheel_create);
}

static void nav_to_pads(void* user_data) {
  (void)user_data;
  menu_navigate_to("Pads", menu_page_pads_create);
}

static void nav_to_expression(void* user_data) {
  (void)user_data;
  menu_navigate_to("Expression", menu_page_expression_create);
}

static void nav_to_cv(void* user_data) {
  (void)user_data;
  menu_navigate_to("Control Voltage", menu_page_cv_scene_create);
}

static void nav_to_proximity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Proximity", menu_page_proximity_scene_create);
}

static void nav_to_ambient_light(void* user_data) {
  (void)user_data;
  menu_navigate_to("Ambient Light", menu_page_als_scene_create);
}

static void nav_to_buttons(void* user_data) {
  (void)user_data;
  menu_navigate_to("Buttons", menu_page_buttons_scene_create);
}

static void nav_to_lfo1(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO1", menu_page_lfo1_scene_create);
}

static void nav_to_lfo2(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO2", menu_page_lfo2_scene_create);
}

static menu_item_t s_lfo_menu_items[2];

static lv_obj_t* lfo_submenu_create(void) {
  s_lfo_menu_items[0] = (menu_item_t){ "LFO1", nav_to_lfo1, NULL, true };
  s_lfo_menu_items[1] = (menu_item_t){ "LFO2", nav_to_lfo2, NULL, true };
  return menu_create_page_2line("LFO", s_lfo_menu_items, 2);
}

static void nav_to_lfo(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO", lfo_submenu_create);
}

// Action config context for bump (single action, skip intermediate page)
static action_config_context_t s_bump_action_ctx;

static void nav_to_bump(void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  s_bump_action_ctx.target_action = &scene->bump;
  s_bump_action_ctx.source_title = "Scene";
  s_bump_action_ctx.detail_title = "Bump";
  s_bump_action_ctx.return_page = menu_page_current_scene_create;
  s_bump_action_ctx.return_depth = 2;  // Pop detail and old Scene, create new Scene
  s_bump_action_ctx.on_complete = NULL;
  s_bump_action_ctx.user_data = NULL;
  s_bump_action_ctx.trigger_type = ACTION_TRIGGER_BUMP;
  
  action_config_start(&s_bump_action_ctx);
}

static void nav_to_on_load(void* user_data) {
  (void)user_data;
  menu_navigate_to("On-Load", menu_page_on_load_scene_create);
}

static void nav_to_on_play(void* user_data) {
  (void)user_data;
  menu_navigate_to("On-Play", menu_page_on_play_scene_create);
}

static void nav_to_rtg(void* user_data) {
  (void)user_data;
  menu_navigate_to("RTG", menu_page_rtg_scene_create);
}

static void nav_to_sample_hold(void* user_data) {
  (void)user_data;
  menu_navigate_to("S+H", menu_page_sample_hold_scene_create);
}

static void nav_to_tilt(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tilt", menu_page_tilt_scene_create);
}

static void nav_to_note_track(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note Track", menu_page_note_track_scene_create);
}

// ============================================================================
// Name Submenu (Regenerate / Edit)
// ============================================================================

static menu_item_t s_name_menu_items[3];
static char s_current_name_display[24];

// Forward declaration
static lv_obj_t* name_submenu_create(void);

static void regenerate_name_action(void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  char new_name[SCENE_NAME_MAX_LEN + 1];
  esp_err_t ret = ESP_ERR_INVALID_ARG;
  // Retry up to 10 times if name collision
  for (int attempt = 0; attempt < 10 && ret == ESP_ERR_INVALID_ARG; attempt++) {
    scene_name_generate(new_name, sizeof(new_name));
    ret = scene_set_name(scene_index, new_name);
  }
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Regenerated scene name: %s", new_name);
  } else {
    ESP_LOGW(TAG, "Failed to regenerate name: %s", esp_err_to_name(ret));
  }
  // Refresh current submenu to show new name, keeping focus on Regenerate (index 1)
  menu_set_restore_focus(1);
  menu_replace_current("Scene Name", name_submenu_create);
}

static void edit_name_action(void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  uint8_t scene_index = scene_get_current_index();
  name_edit_set_name(scene ? scene->name : "", scene_index);
  ui_set_draw_module(&name_edit_module);
}

// Public function for external navigation (e.g., from text editor)
lv_obj_t* menu_page_scene_name_create(void) {
  scene_t* scene = scene_get_current();
  const char* name = (scene && scene->name[0]) ? scene->name : "<unnamed>";
  snprintf(s_current_name_display, sizeof(s_current_name_display), "%s", name);
  
  s_name_menu_items[0] = (menu_item_t){ s_current_name_display, NULL, NULL, false };  // Read-only
  s_name_menu_items[1] = (menu_item_t){ "Regenerate", regenerate_name_action, NULL, false };
  s_name_menu_items[2] = (menu_item_t){ "Edit", edit_name_action, NULL, false };
  return menu_create_page("Scene Name", s_name_menu_items, 3);
}

// Internal alias for forward declaration compatibility
static lv_obj_t* name_submenu_create(void) {
  return menu_page_scene_name_create();
}

static void nav_to_name(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene Name", menu_page_scene_name_create);
}

// ============================================================================
// Helper: Get index base for current device
// ============================================================================

static uint16_t get_device_index_base(void) {
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  if (device && device->pc_info) {
    return device->pc_info->index_base;
  }
  return 0;  // Default to 0-based
}

// ============================================================================
// Configuration Rollers
// ============================================================================

// Preset number roller (respects device index_base)
// Users always see Preset 1, 2, 3... but PC sent depends on indexBase:
// - indexBase=0: Preset 1 → PC 0, Preset 16 → PC 15
// - indexBase=1: Preset 1 → PC 1, Preset 16 → PC 16
static uint16_t s_preset_index_base = 0;  // Cached for confirm callback

static void preset_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  // roller_index 0 = "Preset 1" → PC = 0 + indexBase
  // roller_index 15 = "Preset 16" → PC = 15 + indexBase
  uint8_t pc_value = (uint8_t)(selected_index + s_preset_index_base);
  esp_err_t ret = scene_set_program_number(scene_index, pc_value);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set program number: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* preset_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint16_t index_base = get_device_index_base();
  s_preset_index_base = index_base;  // Cache for confirm callback
  
  uint8_t current_pc = scene ? scene->program_number : index_base;
  // Clamp to valid range (PC must be >= indexBase and <= 127)
  if (current_pc < index_base) current_pc = index_base;
  if (current_pc > 127) current_pc = 127;
  // Convert PC to roller index: PC 0 with indexBase=0 → index 0, PC 1 with indexBase=1 → index 0
  uint32_t roller_index = current_pc - index_base;
  
  // Max preset = 128 - indexBase (so PC values stay 0-127)
  int max_preset = 128 - index_base;
  
  // Build options: Preset 1, 2, 3, ... max_preset (users always see 1-based)
  static char options[640];
  options[0] = '\0';
  for (int preset = 1; preset <= max_preset; preset++) {
    char num[12];
    snprintf(num, sizeof(num), "%d", preset);
    strcat(options, num);
    if (preset < max_preset) strcat(options, "\n");
  }
  
  return menu_create_roller_page("Current Preset", options, roller_index, preset_confirm_cb, NULL);
}

static void nav_to_preset(void* user_data) {
  (void)user_data;
  menu_navigate_to("Preset", preset_roller_create);
}

// PC on load toggle
static void send_pc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  bool send_pc = (selected_index == 0);  // Yes=0, No=1
  esp_err_t ret = scene_set_send_pc_on_load(scene_index, send_pc);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set send PC on load: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* send_pc_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint32_t current = (scene && scene->send_pc_on_load) ? 0 : 1;  // Yes=0, No=1
  
  return menu_create_roller_page("PC on Load", "Yes\nNo", current, send_pc_confirm_cb, NULL);
}

static void nav_to_send_pc(void* user_data) {
  (void)user_data;
  menu_navigate_to("PC on Load", send_pc_roller_create);
}

// Screen roller (UI module selection)
static void screen_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  const char* module_name = (selected_index < (uint32_t)ui_scene_selectable_module_count)
    ? ui_scene_selectable_modules[selected_index] : "beat";
  esp_err_t ret = scene_set_ui_module(scene_index, module_name);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set UI module: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* screen_roller_create(void) {
  const char* current_module = scene_get_ui_module(scene_get_current_index());
  uint32_t current_idx = 0;

  // Build options string from selectable modules (using titles)
  static char options[256];
  options[0] = '\0';
  for (int i = 0; i < ui_scene_selectable_module_count; i++) {
    if (i > 0) strncat(options, "\n", sizeof(options) - strlen(options) - 1);
    const char* name = ui_scene_selectable_modules[i];
    const char* title = ui_get_module_title(name);
    strncat(options, title, sizeof(options) - strlen(options) - 1);
    if (strcmp(name, current_module) == 0)
      current_idx = (uint32_t)i;
  }

  return menu_create_roller_page("Screen", options, current_idx,
    screen_confirm_cb, NULL);
}

static void nav_to_screen(void* user_data) {
  (void)user_data;
  menu_navigate_to("Screen", screen_roller_create);
}

// BPM roller (20-300)
static void bpm_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint16_t bpm = (uint16_t)(selected_index + 20);  // 0 = 20 BPM
  uint8_t scene_index = scene_get_current_index();
  esp_err_t ret = scene_set_bpm(scene_index, bpm);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set BPM: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* bpm_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint16_t current_bpm = scene ? scene->bpm : 120;
  if (current_bpm < 20) current_bpm = 20;
  if (current_bpm > 300) current_bpm = 300;
  uint32_t index = current_bpm - 20;
  
  // Build options string for 20-300
  static char options[1536];
  options[0] = '\0';
  for (int i = 20; i <= 300; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    strcat(options, num);
    if (i < 300) strcat(options, "\n");
  }
  
  return menu_create_roller_page("BPM", options, index, bpm_confirm_cb, NULL);
}

static void nav_to_bpm(void* user_data) {
  (void)user_data;
  menu_navigate_to("BPM", bpm_roller_create);
}

// ============================================================================
// Time Signature - Dual roller on single page
// Numerator (1-16) on left, Denominator (2, 4, 8, 16) on right
// Pad 8 on numerator -> focus denominator
// Pad 12 on denominator -> focus numerator
// Pad 8 on denominator -> save and return
// ============================================================================

static lv_obj_t* s_ts_numer_roller = NULL;
static lv_obj_t* s_ts_denom_roller = NULL;
static bool s_ts_on_denominator = false;  // Which roller has focus

// Called when pad 8 is pressed (via roller click event)
static void time_sig_roller_click_cb(lv_event_t* e) {
  lv_obj_t* roller = lv_event_get_target(e);
  
  if (roller == s_ts_numer_roller) {
    // On numerator, move focus to denominator
    s_ts_on_denominator = true;
    lv_group_t* group = lv_obj_get_group(roller);
    if (group) {
      lv_group_focus_obj(s_ts_denom_roller);
      lv_group_set_editing(group, true);
    }
    ESP_LOGI(TAG, "Time sig: moved to denominator");
  } else if (roller == s_ts_denom_roller) {
    // On denominator, save values and return
    uint32_t numer_idx = lv_roller_get_selected(s_ts_numer_roller);
    uint32_t denom_idx = lv_roller_get_selected(s_ts_denom_roller);
    static const uint8_t denoms[] = {2, 4, 8, 16};
    uint8_t numerator = (uint8_t)(numer_idx + 1);
    uint8_t denominator = denoms[denom_idx];
    uint8_t scene_index = scene_get_current_index();
    esp_err_t ret = scene_set_time_signature(scene_index, numerator, denominator);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set time signature: %s", esp_err_to_name(ret));
    }
    // Clean up and navigate back
    s_ts_numer_roller = NULL;
    s_ts_denom_roller = NULL;
    s_ts_on_denominator = false;
    menu_set_custom_back_handler(NULL);  // Clear custom handler
    menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
  }
}

// Custom back handler for time signature page
// Returns true if handled (don't do normal back navigation)
static bool time_sig_handle_back(void) {
  if (!s_ts_numer_roller || !s_ts_denom_roller) {
    menu_set_custom_back_handler(NULL);  // Clear handler
    return false;
  }
  
  if (s_ts_on_denominator) {
    // On denominator, go back to numerator (don't leave page)
    s_ts_on_denominator = false;
    lv_group_t* group = lv_obj_get_group(s_ts_denom_roller);
    if (group) {
      lv_group_focus_obj(s_ts_numer_roller);
      lv_group_set_editing(group, true);
    }
    ESP_LOGI(TAG, "Time sig: moved back to numerator");
    return true;  // Handled, don't navigate back
  }
  
  // On numerator, allow normal back navigation (cancel)
  s_ts_numer_roller = NULL;
  s_ts_denom_roller = NULL;
  s_ts_on_denominator = false;
  menu_set_custom_back_handler(NULL);  // Clear handler
  return false;  // Not handled, do normal back
}

static lv_obj_t* time_sig_page_create(void) {
  scene_t* scene = scene_get_current();
  
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Reset state
  s_ts_on_denominator = false;
  
  // Find current values
  uint32_t numer_idx = scene ? (scene->time_signature.numerator - 1) : 3;
  if (numer_idx > 15) numer_idx = 3;
  
  static const uint8_t denoms[] = {2, 4, 8, 16};
  uint32_t denom_idx = 1;  // Default to 4
  if (scene) {
    for (size_t i = 0; i < sizeof(denoms)/sizeof(denoms[0]); i++) {
      if (scene->time_signature.denominator == denoms[i]) {
        denom_idx = i;
        break;
      }
    }
  }
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  
  // Title bar
  const int title_bar_h = 22;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, "Time Signature");
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_center(title_label);
  
  // Build numerator options (1-16)
  static char numer_options[64];
  numer_options[0] = '\0';
  for (int i = 1; i <= 16; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    strcat(numer_options, num);
    if (i < 16) strcat(numer_options, "\n");
  }
  
  // Create numerator roller (left side)
  s_ts_numer_roller = lv_roller_create(screen);
  lv_roller_set_options(s_ts_numer_roller, numer_options, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(s_ts_numer_roller, 3);
  lv_roller_set_selected(s_ts_numer_roller, numer_idx, LV_ANIM_OFF);
  lv_obj_align(s_ts_numer_roller, LV_ALIGN_CENTER, -35, 10);
  lv_obj_set_width(s_ts_numer_roller, 50);
  
  // Numerator styling
  lv_obj_set_style_bg_color(s_ts_numer_roller, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ts_numer_roller, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ts_numer_roller, 0, 0);
  lv_obj_set_style_text_color(s_ts_numer_roller, lv_color_make(160, 160, 160), 0);
  lv_obj_set_style_text_font(s_ts_numer_roller, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ts_numer_roller, lv_color_make(60, 60, 60), LV_PART_SELECTED);
  lv_obj_set_style_text_color(s_ts_numer_roller, lv_color_white(), LV_PART_SELECTED);
  
  // Slash separator
  lv_obj_t* slash = lv_label_create(screen);
  lv_label_set_text(slash, "/");
  lv_obj_set_style_text_color(slash, lv_color_white(), 0);
  lv_obj_set_style_text_font(slash, &lv_font_montserrat_14, 0);
  lv_obj_align(slash, LV_ALIGN_CENTER, 0, 10);
  
  // Create denominator roller (right side)
  s_ts_denom_roller = lv_roller_create(screen);
  lv_roller_set_options(s_ts_denom_roller, "2\n4\n8\n16", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(s_ts_denom_roller, 3);
  lv_roller_set_selected(s_ts_denom_roller, denom_idx, LV_ANIM_OFF);
  lv_obj_align(s_ts_denom_roller, LV_ALIGN_CENTER, 35, 10);
  lv_obj_set_width(s_ts_denom_roller, 50);
  
  // Denominator styling
  lv_obj_set_style_bg_color(s_ts_denom_roller, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ts_denom_roller, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ts_denom_roller, 0, 0);
  lv_obj_set_style_text_color(s_ts_denom_roller, lv_color_make(160, 160, 160), 0);
  lv_obj_set_style_text_font(s_ts_denom_roller, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_color(s_ts_denom_roller, lv_color_make(60, 60, 60), LV_PART_SELECTED);
  lv_obj_set_style_text_color(s_ts_denom_roller, lv_color_white(), LV_PART_SELECTED);
  
  // Add click events
  lv_obj_add_event_cb(s_ts_numer_roller, time_sig_roller_click_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_ts_denom_roller, time_sig_roller_click_cb, LV_EVENT_CLICKED, NULL);
  
  // Add both rollers to group, focus numerator first
  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, s_ts_numer_roller);
    lv_group_add_obj(group, s_ts_denom_roller);
    lv_group_focus_obj(s_ts_numer_roller);
    lv_group_set_editing(group, true);
  }
  
  // Set custom back handler for focus switching
  menu_set_custom_back_handler(time_sig_handle_back);
  
  ESP_LOGI(TAG, "Time signature page created: %d/%d", 
    (int)(numer_idx + 1), denoms[denom_idx]);
  
  return screen;
}

static void nav_to_time_sig(void* user_data) {
  (void)user_data;
  menu_navigate_to("Time Signature", time_sig_page_create);
}

// Divider roller (Quarter, Eighth, Sixteenth)
static void divider_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  static const tempo_note_divider_t dividers[] = {
    DIVIDER_QUARTER, DIVIDER_EIGHTH, DIVIDER_SIXTEENTH
  };
  if (selected_index < 3) {
    uint8_t scene_index = scene_get_current_index();
    esp_err_t ret = scene_set_beat_divider(scene_index, dividers[selected_index]);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set beat divider: %s", esp_err_to_name(ret));
    }
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* divider_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint32_t index = 0;
  if (scene) {
    switch (scene->beat_divider) {
      case DIVIDER_QUARTER: index = 0; break;
      case DIVIDER_EIGHTH: index = 1; break;
      case DIVIDER_SIXTEENTH: index = 2; break;
      default: index = 0; break;
    }
  }
  
  return menu_create_roller_page("Divider", 
    "Quarter\nEighth\nSixteenth", index, divider_confirm_cb, NULL);
}

static void nav_to_divider(void* user_data) {
  (void)user_data;
  menu_navigate_to("Divider", divider_roller_create);
}

// Clock source roller (Internal, MIDI, Sync)
static void clock_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  static const tempo_clock_source_t sources[] = {
    CLOCK_SOURCE_INTERNAL, CLOCK_SOURCE_MIDI, CLOCK_SOURCE_SYNC
  };
  if (selected_index < 3) {
    uint8_t scene_index = scene_get_current_index();
    esp_err_t ret = scene_set_clock_source(scene_index, sources[selected_index]);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to set clock source: %s", esp_err_to_name(ret));
    }
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* clock_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint32_t index = 0;
  if (scene) {
    switch (scene->clock_source) {
      case CLOCK_SOURCE_INTERNAL: index = 0; break;
      case CLOCK_SOURCE_MIDI: index = 1; break;
      case CLOCK_SOURCE_SYNC: index = 2; break;
      default: index = 0; break;
    }
  }
  
  return menu_create_roller_page("Clock Source", 
    "Internal\nMIDI\nSync", index, clock_confirm_cb, NULL);
}

static void nav_to_clock(void* user_data) {
  (void)user_data;
  menu_navigate_to("Clock Source", clock_roller_create);
}

// Use transport toggle
static void transport_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  bool use_transport = (selected_index == 0);  // Yes=0, No=1
  esp_err_t ret = scene_set_use_transport(scene_index, use_transport);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set use_transport: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* transport_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint32_t current = (scene && scene->use_transport) ? 0 : 1;  // Yes=0, No=1

  return menu_create_roller_page("Use Transport", "Yes\nNo", current, transport_confirm_cb, NULL);
}

static void nav_to_transport(void* user_data) {
  (void)user_data;
  menu_navigate_to("Use Transport", transport_roller_create);
}

// Send clock toggle
static void send_clock_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  bool send_clock = (selected_index == 0);  // Yes=0, No=1
  esp_err_t ret = scene_set_send_clock(scene_index, send_clock);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set send_clock: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* send_clock_roller_create(void) {
  scene_t* scene = scene_get_current();
  uint32_t current = (scene && scene->send_clock) ? 0 : 1;  // Yes=0, No=1

  return menu_create_roller_page("Send Clock", "Yes\nNo", current, send_clock_confirm_cb, NULL);
}

static void nav_to_send_clock(void* user_data) {
  (void)user_data;
  menu_navigate_to("Send Clock", send_clock_roller_create);
}

// Note channel roller (for routing notes to alternate MIDI channel)
static void note_channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  // Index 0 = "Default (Scene)", 1-16 = channels 1-16
  uint8_t note_channel = (uint8_t)selected_index;
  esp_err_t ret = scene_set_note_channel(scene_index, note_channel);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set note_channel: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* note_channel_roller_create(void) {
  uint8_t current = scene_get_note_channel_setting(scene_get_current_index());
  
  return menu_create_roller_page("Note Channel",
    "Default\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16",
    current,
    note_channel_confirm_cb, NULL);
}

static void nav_to_note_channel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note Channel", note_channel_roller_create);
}

// ============================================================================
// Per-Scene Device Selection (Vendor -> Pedal two-step flow)
// ============================================================================

// Forward declarations
static lv_obj_t* scene_vendor_select_create(void);
static lv_obj_t* scene_pedal_select_create(void);

// Dynamic menu helpers
static void scene_dynamic_menu_free(scene_dynamic_menu_t* menu) {
  if (menu->items) {
    heap_caps_free(menu->items);
    menu->items = NULL;
  }
  if (menu->labels) {
    heap_caps_free(menu->labels);
    menu->labels = NULL;
  }
  if (menu->indices) {
    heap_caps_free(menu->indices);
    menu->indices = NULL;
  }
  menu->count = 0;
  menu->capacity = 0;
}

static bool scene_dynamic_menu_alloc(scene_dynamic_menu_t* menu, uint32_t count) {
  scene_dynamic_menu_free(menu);
  if (count == 0) return true;
  
  menu->items = heap_caps_calloc(count, sizeof(menu_item_t), MALLOC_CAP_SPIRAM);
  menu->labels = heap_caps_calloc(count, 64, MALLOC_CAP_SPIRAM);
  menu->indices = heap_caps_calloc(count, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
  
  if (!menu->items || !menu->labels || !menu->indices) {
    ESP_LOGE(TAG, "Failed to allocate scene dynamic menu for %lu items", (unsigned long)count);
    scene_dynamic_menu_free(menu);
    return false;
  }
  
  menu->capacity = count;
  menu->count = count;
  return true;
}

// Track confirmation flow type (different nav depths for global vs vendor->pedal)
static bool s_from_global_default = false;

// Apply the pending pedal change
static void apply_scene_pedal_change(void) {
  uint8_t scene_index = scene_get_current_index();
  
  if (s_pending_pedal_slug[0] == '\0') {
    scene_clear_device_id(scene_index);
  } else {
    scene_set_device_id(scene_index, s_pending_pedal_slug);
  }
}

// Confirmation page callbacks
static void scene_pedal_change_confirmed(void* user_data) {
  (void)user_data;
  apply_scene_pedal_change();
  // Global Default: back 3 levels (Confirm -> Vendor -> Scene)
  // Vendor->Pedal: back 4 levels (Confirm -> Pedal -> Vendor -> Scene)
  uint8_t depth = s_from_global_default ? 3 : 4;
  menu_navigate_back_then_to(depth, s_page_title, menu_page_current_scene_create);
}

static void pedal_warning_proceed_cb(lv_event_t* e) {
  (void)e;
  scene_pedal_change_confirmed(NULL);
}

static lv_obj_t* scene_pedal_confirm_warning_create(void) {
  uint16_t disp_w = display_get_width();
  uint16_t disp_h = display_get_height();
  
  // Create screen
  lv_obj_t* screen = lv_obj_create(NULL);
  lv_obj_set_size(screen, disp_w, disp_h);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);

  // Title bar
  const int title_bar_h = 32;
  lv_obj_t* title_bar = lv_obj_create(screen);
  lv_obj_set_size(title_bar, disp_w, title_bar_h);
  lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(title_bar, lv_color_make(101, 67, 33), 0);
  lv_obj_set_style_bg_grad_color(title_bar, lv_color_make(139, 90, 43), 0);
  lv_obj_set_style_bg_grad_dir(title_bar, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(title_bar, 0, 0);
  lv_obj_set_style_pad_all(title_bar, 0, 0);
  lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t* title_label = lv_label_create(title_bar);
  lv_label_set_text(title_label, "Pedal Warning");
  lv_obj_set_style_text_color(title_label, lv_color_make(255, 248, 220), 0);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
  lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 8);

  // Content container
  lv_obj_t* cont = lv_obj_create(screen);
  lv_obj_set_size(cont, disp_w, disp_h - title_bar_h);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Warning text - wrapping label
  lv_obj_t* text_label = lv_label_create(cont);
  lv_label_set_text(text_label,
    "Every pedal has different\n"
    "CC mappings, so changing\n"
    "pedals requires manually\n"
    "reassigning parameters.");
  lv_obj_set_style_text_color(text_label, lv_color_make(200, 200, 200), 0);
  lv_obj_set_style_text_font(text_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(text_label, 27, 0);
  lv_obj_set_style_pad_bottom(text_label, 16, 0);

  // Proceed button
  lv_obj_t* btn_label = lv_label_create(cont);
  lv_label_set_text(btn_label, "Proceed?");
  lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(btn_label, lv_color_make(100, 200, 100), LV_STATE_FOCUSED);
  lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_20, LV_STATE_FOCUSED);
  lv_obj_add_flag(btn_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn_label, pedal_warning_proceed_cb, LV_EVENT_CLICKED, NULL);
  
  lv_group_t* group = menu_get_group();
  if (group) {
    lv_group_add_obj(group, btn_label);
    lv_group_focus_obj(btn_label);
  }

  return screen;
}

// Pedal selection callback
static void scene_select_pedal_callback(void* user_data) {
  uint32_t idx = *(uint32_t*)user_data;
  
  const char* slug = NULL;
  const char* name = NULL;
  
  if (assets_get_device_for_vendor(s_scene_selected_vendor, idx, &slug, &name) == ESP_OK && slug) {
    ESP_LOGI(TAG, "Scene pedal selected: %s", slug);
    
    // Check if this is actually a change
    const char* current_device_id = scene_get_device_id(scene_get_current_index());
    bool is_change = true;
    if (current_device_id && current_device_id[0] != '\0') {
      is_change = (strcmp(current_device_id, slug) != 0);
    }
    
    if (!is_change) {
      // Same pedal, just go back
      menu_navigate_back_then_to(3, s_page_title, menu_page_current_scene_create);
      return;
    }
    
    // Store pending slug and show confirmation
    strncpy(s_pending_pedal_slug, slug, sizeof(s_pending_pedal_slug) - 1);
    s_pending_pedal_slug[sizeof(s_pending_pedal_slug) - 1] = '\0';
    s_from_global_default = false;
    menu_navigate_to("Pedal Warning", scene_pedal_confirm_warning_create);
  }
}

// "Global Default" option callback
static void scene_select_global_default_callback(void* user_data) {
  (void)user_data;
  
  // Check if currently using global default
  const char* current_device_id = scene_get_device_id(scene_get_current_index());
  bool is_change = (current_device_id && current_device_id[0] != '\0');
  
  if (!is_change) {
    // Already global default, just go back
    menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
    return;
  }
  
  // Store empty slug for global default and show confirmation
  s_pending_pedal_slug[0] = '\0';
  s_from_global_default = true;
  menu_navigate_to("Pedal Warning", scene_pedal_confirm_warning_create);
}

static lv_obj_t* scene_pedal_select_create(void) {
  ESP_LOGD(TAG, "Creating scene pedal select for vendor: %s", s_scene_selected_vendor);
  
  uint32_t pedal_count = assets_get_device_count_for_vendor(s_scene_selected_vendor);
  
  if (!scene_dynamic_menu_alloc(&s_scene_pedal_menu, pedal_count)) {
    ESP_LOGE(TAG, "Failed to allocate scene pedal menu");
    static menu_item_t error_items[] = {{ "Memory Error", NULL, NULL, false }};
    return menu_create_page("Error", error_items, 1);
  }
  
  for (uint32_t i = 0; i < pedal_count; i++) {
    const char* slug = NULL;
    const char* name = NULL;
    
    if (assets_get_device_for_vendor(s_scene_selected_vendor, i, &slug, &name) == ESP_OK) {
      strncpy(s_scene_pedal_menu.labels[i], name ? name : "Unknown", 63);
      s_scene_pedal_menu.labels[i][63] = '\0';
    } else {
      snprintf(s_scene_pedal_menu.labels[i], 64, "Pedal %u", (unsigned)(i + 1));
    }
    
    s_scene_pedal_menu.indices[i] = i;
    s_scene_pedal_menu.items[i].label = s_scene_pedal_menu.labels[i];
    s_scene_pedal_menu.items[i].callback = scene_select_pedal_callback;
    s_scene_pedal_menu.items[i].user_data = &s_scene_pedal_menu.indices[i];
    s_scene_pedal_menu.items[i].has_submenu = false;
  }
  
  snprintf(s_scene_pedal_title, sizeof(s_scene_pedal_title), "%s", s_scene_selected_vendor);
  
  return menu_create_page(s_scene_pedal_title, s_scene_pedal_menu.items, pedal_count);
}

// Vendor selection callback
static void scene_select_vendor_callback(void* user_data) {
  uint32_t idx = *(uint32_t*)user_data;
  
  const char* vendor = assets_get_vendor_by_index(idx);
  if (vendor) {
    strncpy(s_scene_selected_vendor, vendor, sizeof(s_scene_selected_vendor) - 1);
    s_scene_selected_vendor[sizeof(s_scene_selected_vendor) - 1] = '\0';
    ESP_LOGI(TAG, "Scene vendor selected: %s", s_scene_selected_vendor);
    menu_navigate_to("Select Pedal", scene_pedal_select_create);
  }
}

static lv_obj_t* scene_vendor_select_create(void) {
  ESP_LOGD(TAG, "Creating scene vendor select page");
  
  uint32_t vendor_count = assets_get_vendor_count();
  
  // Find User vendor index and count non-User vendors
  int32_t user_vendor_idx = -1;
  uint32_t non_user_count = 0;
  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* v = assets_get_vendor_by_index(i);
    if (v && strcmp(v, "User") == 0) {
      user_vendor_idx = (int32_t)i;
    } else {
      non_user_count++;
    }
  }
  
  // Structure: Global Default (readonly) + pedal name (clickable) + divider +
  //            User Devices + divider + non-User vendors
  // = 2 + 1 + 1 + 1 + non_user_count = 5 + non_user_count
  uint32_t total_items = 5 + non_user_count;
  
  if (!scene_dynamic_menu_alloc(&s_scene_vendor_menu, total_items)) {
    ESP_LOGE(TAG, "Failed to allocate scene vendor menu");
    static menu_item_t error_items[] = {{ "Memory Error", NULL, NULL, false }};
    return menu_create_page("Error", error_items, 1);
  }
  
  uint32_t idx = 0;
  
  // Item 0: "Global Default" (readonly label)
  strncpy(s_scene_vendor_menu.labels[idx], "Global Default", 63);
  s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
  s_scene_vendor_menu.items[idx].callback = NULL;
  s_scene_vendor_menu.items[idx].user_data = NULL;
  s_scene_vendor_menu.items[idx].has_submenu = false;
  idx++;
  
  // Item 1: Global default pedal name (clickable to select global default)
  const char* global_slug = device_config_get_pedal_slug();
  const manifest_device_t* global_dev = assets_get_manifest_device(global_slug);
  const char* global_name = global_dev ? global_dev->name : "Default";
  strncpy(s_scene_vendor_menu.labels[idx], global_name, 63);
  s_scene_vendor_menu.labels[idx][63] = '\0';
  s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
  s_scene_vendor_menu.items[idx].callback = scene_select_global_default_callback;
  s_scene_vendor_menu.items[idx].user_data = NULL;
  s_scene_vendor_menu.items[idx].has_submenu = false;
  idx++;
  
  // Item 2: Divider
  strncpy(s_scene_vendor_menu.labels[idx], "---", 63);
  s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
  s_scene_vendor_menu.items[idx].callback = NULL;
  s_scene_vendor_menu.items[idx].user_data = NULL;
  s_scene_vendor_menu.items[idx].has_submenu = false;
  idx++;
  
  // Item 3: User Devices
  strncpy(s_scene_vendor_menu.labels[idx], "User Devices", 63);
  s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
  if (user_vendor_idx >= 0) {
    s_scene_vendor_menu.indices[idx] = (uint32_t)user_vendor_idx;
    s_scene_vendor_menu.items[idx].callback = scene_select_vendor_callback;
    s_scene_vendor_menu.items[idx].user_data = &s_scene_vendor_menu.indices[idx];
  } else {
    s_scene_vendor_menu.items[idx].callback = NULL;
    s_scene_vendor_menu.items[idx].user_data = NULL;
  }
  s_scene_vendor_menu.items[idx].has_submenu = true;
  idx++;
  
  // Item 4: Divider
  strncpy(s_scene_vendor_menu.labels[idx], "---", 63);
  s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
  s_scene_vendor_menu.items[idx].callback = NULL;
  s_scene_vendor_menu.items[idx].user_data = NULL;
  s_scene_vendor_menu.items[idx].has_submenu = false;
  idx++;
  
  // Remaining items: vendors except User
  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* vendor = assets_get_vendor_by_index(i);
    if (!vendor || strcmp(vendor, "User") == 0) continue;
    
    strncpy(s_scene_vendor_menu.labels[idx], vendor, 63);
    s_scene_vendor_menu.labels[idx][63] = '\0';
    s_scene_vendor_menu.indices[idx] = i;
    s_scene_vendor_menu.items[idx].label = s_scene_vendor_menu.labels[idx];
    s_scene_vendor_menu.items[idx].callback = scene_select_vendor_callback;
    s_scene_vendor_menu.items[idx].user_data = &s_scene_vendor_menu.indices[idx];
    s_scene_vendor_menu.items[idx].has_submenu = true;
    idx++;
  }
  
  return menu_create_page("Select Pedal", s_scene_vendor_menu.items, idx);
}

static void nav_to_select_pedal(void* user_data) {
  (void)user_data;
  menu_navigate_to("Select Pedal", scene_vendor_select_create);
}

// MIDI Channel roller: Global Default (0), then 1-16
static void midi_channel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  // Index 0 = Global Default (value 0), Index 1-16 = Channels 1-16
  uint8_t channel = (selected_index == 0) ? 0 : (uint8_t)selected_index;
  scene_set_midi_channel(scene_index, channel);
  
  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* midi_channel_roller_create(void) {
  uint8_t current = scene_get_midi_channel(scene_get_current_index());
  // current 0 = index 0, current 1-16 = index 1-16
  uint32_t current_idx = current;
  
  return menu_create_roller_page("MIDI Channel",
    "Global Default\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16",
    current_idx, midi_channel_confirm_cb, NULL);
}

static void nav_to_midi_channel(void* user_data) {
  (void)user_data;
  menu_navigate_to("MIDI Channel", midi_channel_roller_create);
}

// TRS Type roller: Global Default (0), then Type A, Type B, TS, Both
static void trs_type_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint8_t scene_index = scene_get_current_index();
  // Index 0 = Global Default (value 0), Index 1-4 = A/B/TS/Both
  scene_set_trs_type(scene_index, (uint8_t)selected_index);

  // Apply TRS type immediately
  midi_trs_type_t trs = scene_get_effective_trs_type(scene_index);
  midi_transmit_mode_t mode = (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(trs);
  midi_set_uart_transmit_mode(mode);

  menu_navigate_back_then_to(2, s_page_title, menu_page_current_scene_create);
}

static lv_obj_t* trs_type_roller_create(void) {
  uint8_t current = scene_get_trs_type(scene_get_current_index());
  // current 0 = index 0, current 1-4 = index 1-4
  uint32_t current_idx = current;

  return menu_create_roller_page("TRS Polarity",
    "Global Default\nType A\nType B\nTS\nBoth",
    current_idx, trs_type_confirm_cb, NULL);
}

static void nav_to_trs_type(void* user_data) {
  (void)user_data;
  menu_navigate_to("TRS Polarity", trs_type_roller_create);
}

// ============================================================================
// Main Current Scene Page
// ============================================================================

lv_obj_t* menu_page_current_scene_create(void) {
  ESP_LOGD(TAG, "Creating current scene page");
  
  scene_t* scene = scene_get_current();
  uint8_t scene_index = scene_get_current_index();
  scene_mode_t mode = scene_get_mode();
  uint16_t index_base = get_device_index_base();
  
  int idx = 0;
  
  // Build page title: prefix with ordinal in non-Simple modes
  if (mode == SCENE_MODE_SINGLE) {
    snprintf(s_page_title, sizeof(s_page_title), "%s",
      (scene && scene->name[0]) ? scene->name : "Scene");
  } else {
    // Find active ordinal (1-based position among active scenes)
    uint16_t ordinal = 0;
    uint16_t total = scene_get_total_count();
    for (uint16_t i = 0; i < total; i++) {
      if (scene_is_active_by_position(i)) ordinal++;
      if (scene_get_index_by_position(i) == scene_index) break;
    }
    snprintf(s_page_title, sizeof(s_page_title), "%u. %s",
      (unsigned)ordinal,
      (scene && scene->name[0]) ? scene->name : "Untitled");
  }
  
  // Assignment submenus
  s_scene_items[idx++] = (menu_item_t){ "Pads", nav_to_pads, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Touchwheel", nav_to_touchwheel, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Expression", nav_to_expression, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Control Voltage", nav_to_cv, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Proximity", nav_to_proximity, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Ambient Light", nav_to_ambient_light, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Buttons", nav_to_buttons, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "LFO", nav_to_lfo, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Bump", nav_to_bump, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "On-Load", nav_to_on_load, NULL, true };
  if (scene && scene->use_transport) {
    s_scene_items[idx++] = (menu_item_t){ "On-Play", nav_to_on_play, NULL, true };
  }
  s_scene_items[idx++] = (menu_item_t){ "S+H", nav_to_sample_hold, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Tilt", nav_to_tilt, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "RTG", nav_to_rtg, NULL, true };
  s_scene_items[idx++] = (menu_item_t){ "Note Track", nav_to_note_track, NULL, true };
  
  // Per-scene device controls (only in per-scene device mode)
  if (config_get_device_mode() == DEVICE_MODE_PER_SCENE) {
    s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false };
    
    // Get effective device name for display
    const char* effective_slug = scene_get_effective_device_slug(scene_index);
    const manifest_device_t* mdev = assets_get_manifest_device(effective_slug);
    const char* device_name = mdev ? mdev->name : "Default";
    snprintf(s_pedal_label, sizeof(s_pedal_label), "Pedal: %s", device_name);
    s_scene_items[idx++] = (menu_item_t){ s_pedal_label, NULL, NULL, false };
    
    s_scene_items[idx++] = (menu_item_t){ "Select Pedal", nav_to_select_pedal, NULL, true };
    
    // MIDI Channel
    uint8_t midi_ch = scene_get_midi_channel(scene_index);
    if (midi_ch == 0) {
      snprintf(s_midi_channel_label, sizeof(s_midi_channel_label), "MIDI Channel: Global");
    } else {
      snprintf(s_midi_channel_label, sizeof(s_midi_channel_label), "MIDI Channel: %u", (unsigned)midi_ch);
    }
    s_scene_items[idx++] = (menu_item_t){ s_midi_channel_label, nav_to_midi_channel, NULL, false };

    // TRS Polarity
    uint8_t trs = scene_get_trs_type(scene_index);
    const char* trs_names[] = {"Global", "Type A", "Type B", "TS", "Both"};
    snprintf(s_trs_type_label, sizeof(s_trs_type_label), "TRS: %s", 
      trs <= 4 ? trs_names[trs] : "Global");
    s_scene_items[idx++] = (menu_item_t){ s_trs_type_label, nav_to_trs_type, NULL, false };
  }
  
  // Divider
  s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false };
  
  // Scene Name menu item (hidden in Simple mode - only one scene, name less relevant)
  if (mode != SCENE_MODE_SINGLE) {
    s_scene_items[idx++] = (menu_item_t){ "Scene Name", nav_to_name, NULL, true };
  }
  
  // Screen (UI module)
  {
    const char* mod_name = scene_get_ui_module(scene_index);
    const char* title = ui_get_module_title(mod_name);
    snprintf(s_screen_label, sizeof(s_screen_label), "Screen: %s", title);
    s_scene_items[idx++] = (menu_item_t){ s_screen_label, nav_to_screen, NULL, false };
  }
  
  // Preset and PC on load hidden in Preset Sync mode (preset is locked to scene ordinal)
  if (mode != SCENE_MODE_PRESET_SYNC) {
    // Current preset - convert PC to user-friendly 1-based preset number
    // PC 0 with indexBase=0 → Preset 1, PC 16 with indexBase=1 → Preset 16
    // If PC < indexBase (invalid), clamp to minimum valid value (display as 1)
    uint8_t current_pc = scene ? scene->program_number : index_base;
    if (current_pc < index_base) current_pc = index_base;
    int display_preset = current_pc - index_base + 1;
    snprintf(s_preset_label, sizeof(s_preset_label), "Preset: %d", display_preset);
    s_scene_items[idx++] = (menu_item_t){ s_preset_label, nav_to_preset, NULL, false };
    
    // PC on load
    snprintf(s_send_pc_label, sizeof(s_send_pc_label), "PC on load: %s",
      (scene && scene->send_pc_on_load) ? "Yes" : "No");
    s_scene_items[idx++] = (menu_item_t){ s_send_pc_label, nav_to_send_pc, NULL, false };
  }
  
  // BPM
  snprintf(s_bpm_label, sizeof(s_bpm_label), "%d BPM", 
    scene ? scene->bpm : 120);
  s_scene_items[idx++] = (menu_item_t){ s_bpm_label, nav_to_bpm, NULL, false };
  
  // Time signature
  snprintf(s_time_sig_label, sizeof(s_time_sig_label), "%d/%d",
    scene ? scene->time_signature.numerator : 4,
    scene ? scene->time_signature.denominator : 4);
  s_scene_items[idx++] = (menu_item_t){ s_time_sig_label, nav_to_time_sig, NULL, false };
  
  // Divider
  const char* divider_str = "Quarter";
  if (scene) {
    switch (scene->beat_divider) {
      case DIVIDER_QUARTER: divider_str = "Quarter"; break;
      case DIVIDER_EIGHTH: divider_str = "Eighth"; break;
      case DIVIDER_SIXTEENTH: divider_str = "Sixteenth"; break;
      default: break;
    }
  }
  snprintf(s_divider_label, sizeof(s_divider_label), "Divider: %s", divider_str);
  s_scene_items[idx++] = (menu_item_t){ s_divider_label, nav_to_divider, NULL, false };
  
  // Clock source
  const char* clock_str = "Internal";
  if (scene) {
    switch (scene->clock_source) {
      case CLOCK_SOURCE_INTERNAL: clock_str = "Internal"; break;
      case CLOCK_SOURCE_MIDI: clock_str = "MIDI"; break;
      case CLOCK_SOURCE_SYNC: clock_str = "Sync"; break;
      default: break;
    }
  }
  snprintf(s_clock_label, sizeof(s_clock_label), "Clock: %s", clock_str);
  s_scene_items[idx++] = (menu_item_t){ s_clock_label, nav_to_clock, NULL, false };

  // Use transport
  snprintf(s_transport_label, sizeof(s_transport_label), "Transport: %s",
    (scene && scene->use_transport) ? "Yes" : "No");
  s_scene_items[idx++] = (menu_item_t){ s_transport_label, nav_to_transport, NULL, false };

  // Send clock
  snprintf(s_send_clock_label, sizeof(s_send_clock_label), "Send Clock: %s",
    (scene && scene->send_clock) ? "Yes" : "No");
  s_scene_items[idx++] = (menu_item_t){ s_send_clock_label, nav_to_send_clock, NULL, false };

  // Note channel (for routing notes to alternate MIDI channel)
  uint8_t note_ch = scene ? scene->note_channel : 0;
  if (note_ch == 0) {
    snprintf(s_note_channel_label, sizeof(s_note_channel_label), "Note Ch: Default");
  } else {
    snprintf(s_note_channel_label, sizeof(s_note_channel_label), "Note Ch: %d", note_ch);
  }
  s_scene_items[idx++] = (menu_item_t){ s_note_channel_label, nav_to_note_channel, NULL, false };

  ESP_LOGD(TAG, "Current scene page: %d items", idx);
  return menu_create_page(s_page_title, s_scene_items, idx);
}
