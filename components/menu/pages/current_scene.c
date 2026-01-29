#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "tempo.h"
#include "assets_types.h"
#include "display_driver.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CURRENT_SCENE"

// Static storage for menu items and labels
#define MAX_SCENE_ITEMS 24
static menu_item_t s_scene_items[MAX_SCENE_ITEMS];
static char s_scene_title[48];
static char s_page_title[24];
static char s_preset_label[32];
static char s_send_pc_label[24];
static char s_bpm_label[24];
static char s_time_sig_label[16];
static char s_divider_label[24];
static char s_clock_label[24];
static char s_transport_label[24];
static char s_send_clock_label[24];

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
  s_bump_action_ctx.exclude_hold_actions = true;  // No hold actions for bump
  s_bump_action_ctx.on_load_filter = false;
  s_bump_action_ctx.trigger_type = ACTION_TRIGGER_BUMP;
  
  action_config_start(&s_bump_action_ctx);
}

static void nav_to_on_load(void* user_data) {
  (void)user_data;
  menu_navigate_to("On-Load", menu_page_on_load_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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

// BPM roller (20-300)
static void bpm_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  uint16_t bpm = (uint16_t)(selected_index + 20);  // 0 = 20 BPM
  uint8_t scene_index = scene_get_current_index();
  esp_err_t ret = scene_set_bpm(scene_index, bpm);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set BPM: %s", esp_err_to_name(ret));
  }
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
    menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  menu_navigate_back_then_to(1, s_page_title, menu_page_current_scene_create);
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
  
  // Build page title based on mode
  if (mode == SCENE_MODE_SINGLE) {
    snprintf(s_page_title, sizeof(s_page_title), "Scene");
  } else {
    snprintf(s_page_title, sizeof(s_page_title), "Scene %d", scene_index + 1);
  }
  
  // In Preset/Advanced mode, show scene name if defined
  if (mode != SCENE_MODE_SINGLE && scene && scene->name[0]) {
    snprintf(s_scene_title, sizeof(s_scene_title), "%s", scene->name);
    s_scene_items[idx++] = (menu_item_t){ s_scene_title, NULL, NULL, false };
    // Divider after name
    s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false };
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
  
  // Divider
  s_scene_items[idx++] = (menu_item_t){ "---", NULL, NULL, false };
  
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

  ESP_LOGD(TAG, "Current scene page: %d items", idx);
  return menu_create_page(s_page_title, s_scene_items, idx);
}
