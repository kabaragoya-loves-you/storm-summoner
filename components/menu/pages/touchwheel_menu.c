#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "touchwheel_mode_mapping.h"
#include "tempo.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_TOUCHWHEEL"

// Forward declaration
lv_obj_t* menu_page_touchwheel_create(void);

// Helper: Save scene if in programming mode (always true when menu is active)
static void persist_scene_changes(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = scene_get_current_index();
    scene_save_to_flash(scene_index);
  }
}

// ============================================================================
// Static Storage
// ============================================================================

// Maximum items: Mode + 4 CC slots + Style + Base Note + Range + Velocity + Latch + Release + Polyphony + Parameter = ~15
#define MAX_TOUCHWHEEL_ITEMS 16
static menu_item_t s_tw_items[MAX_TOUCHWHEEL_ITEMS];
static char s_mode_label[32];
static char s_style_label[32];

// Re-entry guard to prevent callbacks from being processed multiple times
static bool s_callback_in_progress = false;

static char s_cc_slot_labels[MAX_MULTI_CC][48];
static char s_base_note_label[32];
static char s_range_label[32];
static char s_velocity_label[32];
static char s_latch_label[32];
static char s_release_label[32];
static char s_polyphony_label[32];
static char s_initial_value_label[32];
static char s_lfo_target_label[32];

// Dynamic storage for CC options from device
typedef struct {
  char* options_str;       // Newline-separated options for roller
  uint8_t* cc_numbers;     // CC numbers corresponding to each option
  uint16_t count;          // Number of CC options
} cc_options_t;

static cc_options_t s_cc_options = {0};

// ============================================================================
// Mode Mapping (uses shared definitions from touchwheel_modes.h)
// ============================================================================

#define NUM_BASE_MODES NUM_TOUCHWHEEL_USER_MODES

static bool is_touchwheel_disabled(const scene_t* scene) {
  return scene && scene->touchwheel_mode != TOUCHWHEEL_MODE_PADS && !scene->touchwheel.enabled;
}

static const char* touchwheel_lfo_target_label(lfo_target_t target) {
  switch (target) {
    case LFO_TARGET_LFO1: return "LFO 1";
    case LFO_TARGET_LFO2: return "LFO 2";
    default: return "Both";
  }
}

// Get current mode mapping index from the live scene (falls back to JSON on disk)
static int get_current_mode_index(void) {
  scene_t* scene = scene_get_current();
  if (scene) return (int)touchwheel_get_current_mode_index(scene);

  scene_t fallback = {
    .touchwheel_mode = scene_get_persisted_touchwheel_mode(scene_get_current_index()),
  };
  fallback.touchwheel.output_type =
    scene_get_persisted_touchwheel_output_type(scene_get_current_index());
  return (int)touchwheel_get_current_mode_index(&fallback);
}

// ============================================================================
// CC Options Loading (from device definition)
// ============================================================================

static void free_cc_options(void) {
  if (s_cc_options.options_str) {
    heap_caps_free(s_cc_options.options_str);
    s_cc_options.options_str = NULL;
  }
  if (s_cc_options.cc_numbers) {
    heap_caps_free(s_cc_options.cc_numbers);
    s_cc_options.cc_numbers = NULL;
  }
  s_cc_options.count = 0;
}

static bool load_cc_options(void) {
  free_cc_options();
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  if (!device || device->control_count == 0) {
    ESP_LOGD(TAG, "No device or no controls available");
    return false;
  }
  
  // Allocate storage (+1 for "Inactive" option)
  uint16_t total = device->control_count + 1;
  s_cc_options.cc_numbers = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  
  // Estimate string size: avg 20 chars per name + newlines
  size_t str_size = total * 24;
  s_cc_options.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_cc_options.cc_numbers || !s_cc_options.options_str) {
    ESP_LOGE(TAG, "Failed to allocate CC options");
    free_cc_options();
    return false;
  }
  
  // Build options string: "Inactive\nName1\nName2..."
  strcpy(s_cc_options.options_str, "Inactive");
  s_cc_options.cc_numbers[0] = 0xFF;  // Sentinel for "Inactive"
  
  size_t pos = strlen("Inactive");
  for (uint16_t i = 0; i < device->control_count && (pos + 24) < str_size; i++) {
    const midi_control_t* ctrl = &device->controls[i];
    if (ctrl->type == MIDI_CONTROL_TYPE_CC) {
      s_cc_options.options_str[pos++] = '\n';
      const char* name = ctrl->name ? ctrl->name : "Unknown";
      size_t name_len = strlen(name);
      if (name_len > 22) name_len = 22;  // Truncate long names
      memcpy(s_cc_options.options_str + pos, name, name_len);
      pos += name_len;
      s_cc_options.options_str[pos] = '\0';
      
      s_cc_options.cc_numbers[s_cc_options.count + 1] = (uint8_t)ctrl->id;
      s_cc_options.count++;
    }
  }
  s_cc_options.count++;  // Include "Inactive"
  
  ESP_LOGD(TAG, "Loaded %u CC options", (unsigned)s_cc_options.count);
  return true;
}

// Find option index for a CC number (0 = Inactive)
static uint32_t cc_number_to_option_index(uint8_t cc_num) {
  if (cc_num == 0xFF) return 0;  // Inactive
  
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    if (s_cc_options.cc_numbers[i] == cc_num) {
      return i;
    }
  }
  return 0;  // Not found = Inactive
}

// ============================================================================
// Mode Roller
// ============================================================================

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  if (selected_index == 0) {
    scene->touchwheel.enabled = false;
    persist_scene_changes();
    s_callback_in_progress = false;
    menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
    return;
  }

  // Convert roller index to mapping index (accounting for Disabled + skipped modes)
  bool show_tempo = (scene->clock_source == CLOCK_SOURCE_INTERNAL);
  scene_mode_t scene_mode = scene_get_mode();
  bool show_program_change = (scene_mode != SCENE_MODE_PRESET_SYNC);
  uint32_t mapping_index = 0;
  uint32_t roller_count = 0;
  uint32_t adjusted_index = selected_index - 1;
  bool mapping_found = false;
  
  for (size_t i = 0; i < NUM_BASE_MODES; i++) {
    // Skip Tempo if clock is not internal (same logic as mode_roller_create)
    if (g_touchwheel_mode_mappings[i].mode == TOUCHWHEEL_MODE_SET_TEMPO && !show_tempo) {
      continue;
    }
    // Skip Program Change in Preset Sync mode (same logic as mode_roller_create)
    if (g_touchwheel_mode_mappings[i].mode == TOUCHWHEEL_MODE_PROGRAM_CHANGE && !show_program_change) {
      continue;
    }
    if (roller_count == adjusted_index) {
      mapping_index = (uint32_t)i;
      mapping_found = true;
      break;
    }
    roller_count++;
  }
  
  if (!mapping_found || mapping_index >= NUM_BASE_MODES) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  const touchwheel_mode_mapping_t* mapping = &g_touchwheel_mode_mappings[mapping_index];
  if (mapping->mode == TOUCHWHEEL_MODE_SET_TEMPO) {
    if (scene->clock_source != CLOCK_SOURCE_INTERNAL) {
      ESP_LOGW(TAG, "Tempo mode requires internal clock");
      s_callback_in_progress = false;
      menu_navigate_back();
      return;
    }
  }
  
  // Set output type if needed (for CC vs Notes)
  if (mapping->use_output_type) {
    scene->touchwheel.output_type = mapping->output_type;
  }
  
  // Set style BEFORE calling scene_set_touchwheel_mode, since that function
  // uses the style to determine which mode processor to create
  scene->touchwheel_style = mapping->default_style;
  
  // Enable touchwheel for all modes except Pads
  scene->touchwheel.enabled = (mapping->mode != TOUCHWHEEL_MODE_PADS);
  
  // Apply the mode (this will set up the touchwheel using the style we just set)
  uint8_t scene_index = scene_get_current_index();
  scene_set_touchwheel_mode(scene_index, mapping->mode);
  
  // Persist all changes (mode change already persisted, but output_type etc aren't)
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Touchwheel mode set to: %s", mapping->display_name);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  bool show_tempo = (scene && scene->clock_source == CLOCK_SOURCE_INTERNAL);
  scene_mode_t scene_mode = scene_get_mode();
  bool show_program_change = (scene_mode != SCENE_MODE_PRESET_SYNC);
  
  // Build options string, conditionally including Tempo and Program Change
  static char options[256];
  options[0] = '\0';
  
  int current_mode_idx = get_current_mode_index();
  uint32_t roller_index = is_touchwheel_disabled(scene) ? 0 : 1;
  uint32_t option_count = 1;
  strcpy(options, "Disabled");
  
  for (size_t i = 0; i < NUM_BASE_MODES; i++) {
    // Skip Tempo if clock is not internal
    if (g_touchwheel_mode_mappings[i].mode == TOUCHWHEEL_MODE_SET_TEMPO && !show_tempo) {
      continue;
    }
    // Skip Program Change in Preset Sync mode (presets tied to scenes)
    if (g_touchwheel_mode_mappings[i].mode == TOUCHWHEEL_MODE_PROGRAM_CHANGE && !show_program_change) {
      continue;
    }
    
    strcat(options, "\n");
    strcat(options, g_touchwheel_mode_mappings[i].display_name);
    
    if (!is_touchwheel_disabled(scene) && (int)i == current_mode_idx) {
      roller_index = option_count;
    }
    option_count++;
  }
  
  return menu_create_roller_page("Mode", options, roller_index, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Style Roller
// ============================================================================

static const char* style_to_string(touchwheel_style_t style) {
  switch (style) {
    case TOUCHWHEEL_STYLE_ENDLESS: return "Endless";
    case TOUCHWHEEL_STYLE_ODOMETER: return "Odometer";
    case TOUCHWHEEL_STYLE_BIPOLAR: return "Bipolar";
    default: return "Unknown";
  }
}

static void style_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Map index to style - all modes now just use Endless/Odometer
  scene->touchwheel_style = (selected_index == 0) ? 
    TOUCHWHEEL_STYLE_ENDLESS : TOUCHWHEEL_STYLE_ODOMETER;
  
  int mode_idx = get_current_mode_index();
  touchwheel_mode_t mode = g_touchwheel_mode_mappings[mode_idx].mode;
  
  // Re-setup the touchwheel to apply the new style
  uint8_t scene_index = scene_get_current_index();
  scene_set_touchwheel_mode(scene_index, mode);
  
  ESP_LOGI(TAG, "Style set to: %s", style_to_string(scene->touchwheel_style));
  
  s_callback_in_progress = false;
  // Go back 2 levels: pop roller AND old Touchwheel, then push new Touchwheel
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* style_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  int mode_idx = get_current_mode_index();
  touchwheel_mode_t mode = g_touchwheel_mode_mappings[mode_idx].mode;
  output_type_t output = scene->touchwheel.output_type;
  
  const char* options;
  uint32_t current_idx = 0;
  
  // All modes now just use Endless/Odometer (Bipolar removed from Notes)
  {
    (void)mode;  // Unused now
    (void)output;
    // Other modes: Endless/Odometer only
    options = "Endless\nOdometer";
    current_idx = (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? 0 : 1;
  }
  
  return menu_create_roller_page("Style", options, current_idx, style_confirm_cb, NULL);
}

static void nav_to_style(void* user_data) {
  (void)user_data;
  menu_navigate_to("Style", style_roller_create);
}

// ============================================================================
// LFO Target Roller (LFO Rate / LFO Depth modes)
// ============================================================================

static void touchwheel_lfo_target_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->touchwheel_lfo_target = (lfo_target_t)selected_index;
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* touchwheel_lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (uint32_t)scene->touchwheel_lfo_target;
  return menu_create_roller_page("Target", "LFO 1\nLFO 2\nBoth", current,
    touchwheel_lfo_target_confirm_cb, NULL);
}

static void nav_to_touchwheel_lfo_target(void* user_data) {
  (void)user_data;
  menu_navigate_to("Target", touchwheel_lfo_target_roller_create);
}

// ============================================================================
// Touchwheel Tempo Floor / Ceiling Rollers
// ============================================================================

static char s_tempo_floor_label[32];
static char s_tempo_ceiling_label[32];

static void touchwheel_tempo_bound_confirm_cb(uint32_t selected_index, void* user_data) {
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  uint16_t bpm = (uint16_t)(selected_index + 20);
  bool is_floor = (user_data != NULL);
  if (is_floor) {
    scene->touchwheel_tempo_floor = bpm;
    if (scene->touchwheel_tempo_floor > scene->touchwheel_tempo_ceiling) {
      scene->touchwheel_tempo_ceiling = scene->touchwheel_tempo_floor;
    }
  } else {
    scene->touchwheel_tempo_ceiling = bpm;
    if (scene->touchwheel_tempo_ceiling < scene->touchwheel_tempo_floor) {
      scene->touchwheel_tempo_floor = scene->touchwheel_tempo_ceiling;
    }
  }
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* touchwheel_tempo_bound_roller_create(void* user_data) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  static char options[4096];
  char* pos = options;
  size_t remaining = sizeof(options);
  options[0] = '\0';
  for (unsigned bpm = 20; bpm <= 300; bpm++) {
    int n = snprintf(pos, remaining, "%s%u", bpm > 20 ? "\n" : "", bpm);
    if (n <= 0 || (size_t)n >= remaining) break;
    pos += n;
    remaining -= (size_t)n;
  }

  bool is_floor = (user_data != NULL);
  uint16_t current = is_floor ? scene->touchwheel_tempo_floor : scene->touchwheel_tempo_ceiling;
  if (current < 20 || current > 300) current = is_floor ? 20 : 300;

  return menu_create_roller_page(is_floor ? "Floor" : "Ceiling", options,
    (uint32_t)(current - 20), touchwheel_tempo_bound_confirm_cb, user_data);
}

static lv_obj_t* touchwheel_tempo_floor_roller_create(void) {
  return touchwheel_tempo_bound_roller_create((void*)1);
}

static lv_obj_t* touchwheel_tempo_ceiling_roller_create(void) {
  return touchwheel_tempo_bound_roller_create(NULL);
}

static void nav_to_touchwheel_tempo_floor(void* user_data) {
  (void)user_data;
  menu_navigate_to("Floor", touchwheel_tempo_floor_roller_create);
}

static void nav_to_touchwheel_tempo_ceiling(void* user_data) {
  (void)user_data;
  menu_navigate_to("Ceiling", touchwheel_tempo_ceiling_roller_create);
}

// ============================================================================
// Tempo Nudge % Roller
// ============================================================================

static char s_nudge_label[32];
static char s_return_speed_label[32];

static const char* return_speed_to_string(uint8_t speed) {
  switch (speed) {
    case TOUCHWHEEL_NUDGE_RETURN_FAST: return "Fast";
    case TOUCHWHEEL_NUDGE_RETURN_MEDIUM: return "Medium";
    case TOUCHWHEEL_NUDGE_RETURN_SLOW: return "Slow";
    default: return "Instant";
  }
}

static void return_speed_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  if (selected_index > TOUCHWHEEL_NUDGE_RETURN_SLOW) selected_index = TOUCHWHEEL_NUDGE_RETURN_INSTANT;
  scene_set_touchwheel_tempo_nudge_return(scene_get_current_index(), (uint8_t)selected_index);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* return_speed_roller_create(void) {
  uint8_t cur = scene_get_touchwheel_tempo_nudge_return(scene_get_current_index());
  if (cur > TOUCHWHEEL_NUDGE_RETURN_SLOW) cur = TOUCHWHEEL_NUDGE_RETURN_INSTANT;
  return menu_create_roller_page("Return Speed", "Instant\nFast\nMedium\nSlow",
    (uint32_t)cur, return_speed_confirm_cb, NULL);
}

static void nav_to_return_speed(void* user_data) {
  (void)user_data;
  menu_navigate_to("Return Speed", return_speed_roller_create);
}

static void nudge_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  uint8_t pct = (uint8_t)(selected_index * 5);
  if (pct > 100) pct = 100;
  scene_set_touchwheel_tempo_nudge_pct(scene_get_current_index(), pct);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* nudge_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 0; v <= 100; v += 5) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%%s", v, v < 100 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }

  uint8_t cur = scene_get_touchwheel_tempo_nudge_pct(scene_get_current_index());
  if (cur > 100) cur = 100;
  uint32_t idx = cur / 5;
  return menu_create_roller_page("Nudge %", options, idx, nudge_confirm_cb, NULL);
}

static void nav_to_nudge(void* user_data) {
  (void)user_data;
  menu_navigate_to("Nudge %", nudge_roller_create);
}

// ============================================================================
// CC Slot Rollers (for Control Change mode)
// ============================================================================

static uint8_t s_editing_cc_slot = 0;  // Which slot (0-3) we're editing

static void cc_slot_confirm_cb(uint32_t selected_index, void* user_data) {
  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Get slot from user_data (more reliable than static variable)
  uint8_t slot = (uint8_t)(uintptr_t)user_data;
  ESP_LOGI(TAG, "CC slot confirm: slot=%u (static=%u), selected=%lu",
           (unsigned)slot, (unsigned)s_editing_cc_slot, (unsigned long)selected_index);
  
  if (selected_index == 0) {
    // "Inactive" selected - set this slot to 0
    scene->touchwheel.cc_numbers[slot] = 0;
  } else {
    // CC selected - set directly in the slot position
    scene->touchwheel.cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }
  
  // Recalculate num_cc_numbers (count of non-zero slots)
  scene->touchwheel.num_cc_numbers = 0;
  for (int i = 0; i < MAX_MULTI_CC; i++) {
    if (scene->touchwheel.cc_numbers[i] > 0) {
      scene->touchwheel.num_cc_numbers++;
    }
  }
  
  persist_scene_changes();
  
  ESP_LOGI(TAG, "CC slot %u updated, now %u active CCs", 
           (unsigned)slot, (unsigned)scene->touchwheel.num_cc_numbers);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene || !s_cc_options.options_str) {
    return NULL;
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;  // Default to "Inactive"
  
  // Check if this slot has a CC configured (non-zero value)
  if (slot < MAX_MULTI_CC && scene->touchwheel.cc_numbers[slot] > 0) {
    current_idx = cc_number_to_option_index(scene->touchwheel.cc_numbers[slot]);
  }
  
  static char title[32];
  int mode_idx = get_current_mode_index();
  const char* slot_label = (g_touchwheel_mode_mappings[mode_idx].mode == TOUCHWHEEL_MODE_DOUBLE_CC)
    ? "Parameter" : "Control Change";
  snprintf(title, sizeof(title), "%s %u", slot_label, (unsigned)(slot + 1));
  
  ESP_LOGI(TAG, "Creating CC slot roller: slot=%u, current_idx=%lu", 
           (unsigned)slot, (unsigned long)current_idx);
  
  // Pass slot through user_data for reliable callback
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx, 
                                  cc_slot_confirm_cb, (void*)(uintptr_t)slot);
}

static void nav_to_cc_slot(void* user_data) {
  s_editing_cc_slot = (uint8_t)(uintptr_t)user_data;
  
  // Ensure CC options are loaded
  if (!s_cc_options.options_str) {
    load_cc_options();
  }
  
  static char title[32];
  snprintf(title, sizeof(title), "Control Change %u", (unsigned)(s_editing_cc_slot + 1));
  menu_navigate_to(title, cc_slot_roller_create);
}

// ============================================================================
// Notes Mode Rollers
// ============================================================================

// Note name helper
static const char* s_note_names[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void get_note_name(uint8_t midi_note, char* buf, size_t buf_size) {
  int octave = (midi_note / 12) - 1;  // MIDI note 0 = C-1
  int note_idx = midi_note % 12;
  snprintf(buf, buf_size, "%s%d", s_note_names[note_idx], octave);
}

static void base_note_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->touchwheel.base_note = (uint8_t)selected_index;
  persist_scene_changes();

  ESP_LOGI(TAG, "Base note set to: %u", (unsigned)selected_index);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* base_note_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Build note options: C-1 (0) through G9 (127)
  static char options[1024];
  options[0] = '\0';
  
  for (int i = 0; i <= 127; i++) {
    char note_name[8];
    get_note_name((uint8_t)i, note_name, sizeof(note_name));
    if (i > 0) strcat(options, "\n");
    strcat(options, note_name);
  }
  
  uint32_t current = scene->touchwheel.base_note;
  if (current > 127) current = 60;  // Default to middle C
  
  return menu_create_roller_page("Base Note", options, current, base_note_confirm_cb, NULL);
}

static void nav_to_base_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Base Note", base_note_roller_create);
}

// Range (in octaves)
static void range_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // selected_index 0 = 1 octave = 12 semitones
  scene->touchwheel.note_range = (uint8_t)((selected_index + 1) * 12);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Note range set to: %u semitones (%u octaves)", 
           (unsigned)scene->touchwheel.note_range, (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* range_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // 1-10 octaves
  static const char* options = 
    "1 Octave\n2 Octaves\n3 Octaves\n4 Octaves\n5 Octaves\n"
    "6 Octaves\n7 Octaves\n8 Octaves\n9 Octaves\n10 Octaves";
  
  uint8_t range = scene->touchwheel.note_range;
  uint32_t current = (range / 12);
  if (current > 0) current--;  // 12 semitones = index 0
  if (current > 9) current = 0;  // Clamp
  
  return menu_create_roller_page("Range", options, current, range_confirm_cb, NULL);
}

static void nav_to_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Range", range_roller_create);
}

// Velocity
static void velocity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  // Prevent re-entry
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // selected_index 0 = velocity 1
  scene->touchwheel.velocity = (uint8_t)(selected_index + 1);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Velocity set to: %u", (unsigned)scene->touchwheel.velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* velocity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Build options 1-127
  static char options[640];
  options[0] = '\0';
  for (int i = 1; i <= 127; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, num);
  }
  
  uint8_t vel = scene->touchwheel.velocity;
  if (vel == 0) vel = 100;  // Default
  uint32_t current = vel - 1;  // Convert to 0-based index
  
  return menu_create_roller_page("Velocity", options, current, velocity_confirm_cb, NULL);
}

static void nav_to_velocity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity", velocity_roller_create);
}

// Initial Value (0-127 for CC endless mode)
static void initial_value_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->touchwheel_initial_value = (uint8_t)selected_index;
  scene_set_touchwheel_value((uint8_t)selected_index);
  persist_scene_changes();

  ESP_LOGI(TAG, "Touchwheel initial value set to: %u", (unsigned)selected_index);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* initial_value_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  static char options[640];
  char* p = options;
  for (int i = 0; i <= 127; i++) {
    int written = snprintf(p, options + sizeof(options) - p, "%d\n", i);
    p += written;
  }
  if (p > options) *(p - 1) = '\0';

  uint32_t current = scene->touchwheel_initial_value;
  if (current > 127) current = 0;

  return menu_create_roller_page("Initial Value", options, current, initial_value_confirm_cb, NULL);
}

static void nav_to_initial_value(void* user_data) {
  (void)user_data;
  menu_navigate_to("Initial Value", initial_value_roller_create);
}

// Latch (On/Off)
static void latch_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->touchwheel.note_latch = (selected_index == 1);  // 0=Off, 1=On
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Latch set to: %s", scene->touchwheel.note_latch ? "On" : "Off");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* latch_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = scene->touchwheel.note_latch ? 1 : 0;
  return menu_create_roller_page("Latch", "Off\nOn", current, latch_confirm_cb, NULL);
}

static void nav_to_latch(void* user_data) {
  (void)user_data;
  menu_navigate_to("Latch", latch_roller_create);
}

// Release (100-4000ms, only when latch is on)
static void release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Index 0 = 100ms, index 39 = 4000ms (100ms steps)
  scene->touchwheel.note_release_ms = (uint16_t)((selected_index + 1) * 100);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Release set to: %u ms", (unsigned)scene->touchwheel.note_release_ms);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* release_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Build options: 100ms to 4000ms in 100ms steps
  static char options[512];
  options[0] = '\0';
  for (int i = 100; i <= 4000; i += 100) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d ms", i);
    if (i > 100) strcat(options, "\n");
    strcat(options, buf);
  }
  
  uint16_t release = scene->touchwheel.note_release_ms;
  if (release < 100) release = 500;  // Default
  uint32_t current = (release / 100) - 1;
  if (current > 39) current = 4;  // Default to 500ms
  
  return menu_create_roller_page("Release", options, current, release_confirm_cb, NULL);
}

static void nav_to_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", release_roller_create);
}

// Polyphony (Mono/Poly, only when style is Odometer)
static void polyphony_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->touchwheel.polyphony = (selected_index == 0) ? POLYPHONY_MONO : POLYPHONY_POLY;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Polyphony set to: %s", 
    scene->touchwheel.polyphony == POLYPHONY_MONO ? "Mono" : "Poly");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Touchwheel", menu_page_touchwheel_create);
}

static lv_obj_t* polyphony_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (scene->touchwheel.polyphony == POLYPHONY_MONO) ? 0 : 1;
  return menu_create_roller_page("Polyphony", "Mono\nPoly", current, polyphony_confirm_cb, NULL);
}

static void nav_to_polyphony(void* user_data) {
  (void)user_data;
  menu_navigate_to("Polyphony", polyphony_roller_create);
}

// ============================================================================
// Main Touchwheel Menu Page
// ============================================================================

static const char* get_cc_slot_display(scene_t* scene, uint8_t slot) {
  if (!scene || slot >= MAX_MULTI_CC) {
    return "Inactive";
  }
  
  uint8_t cc_num = scene->touchwheel.cc_numbers[slot];
  
  // 0 means inactive (slot not configured)
  if (cc_num == 0) {
    return "Inactive";
  }
  
  // Try to find the name from device
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  if (device && device->cc_lookup) {
    int16_t idx = device->cc_lookup[cc_num];
    if (idx >= 0 && idx < device->control_count) {
      return device->controls[idx].name ? device->controls[idx].name : "Unknown";
    }
  }
  
  // Fallback to CC number
  static char fallback[24];
  snprintf(fallback, sizeof(fallback), "CC %u", (unsigned)cc_num);
  return fallback;
}

lv_obj_t* menu_page_touchwheel_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    ESP_LOGW(TAG, "No current scene");
    return menu_create_page_2line("Touchwheel", NULL, 0);
  }

  int mode_idx = get_current_mode_index();
  const touchwheel_mode_mapping_t* mapping = &g_touchwheel_mode_mappings[mode_idx];
  
  int item_count = 0;
  
  if (is_touchwheel_disabled(scene)) {
    snprintf(s_mode_label, sizeof(s_mode_label), "Mode\nDisabled");
  } else {
    snprintf(s_mode_label, sizeof(s_mode_label), "Mode\n%s", mapping->display_name);
  }
  s_tw_items[item_count++] = (menu_item_t){
    s_mode_label, nav_to_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (is_touchwheel_disabled(scene)) {
    return menu_create_page_2line("Touchwheel", s_tw_items, item_count);
  }
  
  // Mode-specific items
  switch (mapping->mode) {
    case TOUCHWHEEL_MODE_PADS:
      // No additional items for Pads mode
      break;
      
    case TOUCHWHEEL_MODE_CONTINUOUS:
      if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
        uint8_t pct = scene_get_touchwheel_tempo_nudge_pct(scene_get_current_index());
        snprintf(s_nudge_label, sizeof(s_nudge_label), "Nudge %%\n%u%%", (unsigned)pct);
        s_tw_items[item_count++] = (menu_item_t){s_nudge_label, nav_to_nudge, NULL, true, MENU_ITEM_KIND_ROLLER};
        uint8_t ret_spd = scene_get_touchwheel_tempo_nudge_return(scene_get_current_index());
        snprintf(s_return_speed_label, sizeof(s_return_speed_label), "Return Speed\n%s",
          return_speed_to_string(ret_spd));
        s_tw_items[item_count++] = (menu_item_t){
          s_return_speed_label, nav_to_return_speed, NULL, true, MENU_ITEM_KIND_ROLLER
        };
      } else if (mapping->output_type == OUTPUT_TYPE_CC) {
        // Control Change mode: 4 CC slots + Style (2-line format)
        for (int i = 0; i < MAX_MULTI_CC; i++) {
          const char* cc_name = get_cc_slot_display(scene, (uint8_t)i);
          snprintf(s_cc_slot_labels[i], sizeof(s_cc_slot_labels[i]), 
                   "Control Change %d\n%s", i + 1, cc_name);
          s_tw_items[item_count++] = (menu_item_t){
            s_cc_slot_labels[i], nav_to_cc_slot, (void*)(uintptr_t)i, true, MENU_ITEM_KIND_ROLLER
          };
        }
      } else {
        // Notes mode: Base Note, Range, Velocity, Latch, [Release], [Polyphony] (2-line format)
        char note_name[8];
        get_note_name(scene->touchwheel.base_note, note_name, sizeof(note_name));
        snprintf(s_base_note_label, sizeof(s_base_note_label), "Base Note\n%s", note_name);
        s_tw_items[item_count++] = (menu_item_t){s_base_note_label, nav_to_base_note, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        uint8_t octaves = scene->touchwheel.note_range / 12;
        if (octaves == 0) octaves = 1;
        snprintf(s_range_label, sizeof(s_range_label), "Range\n%u Octave%s", 
                 (unsigned)octaves, octaves > 1 ? "s" : "");
        s_tw_items[item_count++] = (menu_item_t){s_range_label, nav_to_range, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        uint8_t vel = scene->touchwheel.velocity;
        if (vel == 0) vel = 100;
        snprintf(s_velocity_label, sizeof(s_velocity_label), "Velocity\n%u", (unsigned)vel);
        s_tw_items[item_count++] = (menu_item_t){s_velocity_label, nav_to_velocity, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        // Latch
        snprintf(s_latch_label, sizeof(s_latch_label), "Latch\n%s", 
                 scene->touchwheel.note_latch ? "On" : "Off");
        s_tw_items[item_count++] = (menu_item_t){s_latch_label, nav_to_latch, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        // Release (only shown when Latch is On)
        if (scene->touchwheel.note_latch) {
          uint16_t release = scene->touchwheel.note_release_ms;
          if (release < 100) release = 500;
          snprintf(s_release_label, sizeof(s_release_label), "Release\n%u ms", (unsigned)release);
          s_tw_items[item_count++] = (menu_item_t){s_release_label, nav_to_release, NULL, true, MENU_ITEM_KIND_ROLLER};
        }
        
        // Polyphony (only shown when style is Odometer)
        if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ODOMETER) {
          snprintf(s_polyphony_label, sizeof(s_polyphony_label), "Polyphony\n%s",
                   scene->touchwheel.polyphony == POLYPHONY_MONO ? "Mono" : "Poly");
          s_tw_items[item_count++] = (menu_item_t){s_polyphony_label, nav_to_polyphony, NULL, true, MENU_ITEM_KIND_ROLLER};
        }
      }
      break;
      
    case TOUCHWHEEL_MODE_DOUBLE_CC:
      for (int i = 0; i < MAX_MULTI_CC; i++) {
        const char* cc_name = get_cc_slot_display(scene, (uint8_t)i);
        snprintf(s_cc_slot_labels[i], sizeof(s_cc_slot_labels[i]),
                 "Parameter %d\n%s", i + 1, cc_name);
        s_tw_items[item_count++] = (menu_item_t){
          s_cc_slot_labels[i], nav_to_cc_slot, (void*)(uintptr_t)i, true, MENU_ITEM_KIND_ROLLER
        };
      }
      break;
      
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE:
    case TOUCHWHEEL_MODE_PITCH_BEND:
    case TOUCHWHEEL_MODE_VELOCITY:
      // No additional items for these modes
      break;
      
    case TOUCHWHEEL_MODE_SET_TEMPO:
      snprintf(s_tempo_floor_label, sizeof(s_tempo_floor_label), "Floor\n%u BPM",
        (unsigned)scene->touchwheel_tempo_floor);
      s_tw_items[item_count++] = (menu_item_t){
        s_tempo_floor_label, nav_to_touchwheel_tempo_floor, NULL, true, MENU_ITEM_KIND_ROLLER
      };
      snprintf(s_tempo_ceiling_label, sizeof(s_tempo_ceiling_label), "Ceiling\n%u BPM",
        (unsigned)scene->touchwheel_tempo_ceiling);
      s_tw_items[item_count++] = (menu_item_t){
        s_tempo_ceiling_label, nav_to_touchwheel_tempo_ceiling, NULL, true, MENU_ITEM_KIND_ROLLER
      };
      break;

    case TOUCHWHEEL_MODE_AFTERTOUCH:
      // Style selection added below
      break;
      
    case TOUCHWHEEL_MODE_LFO_RATE:
    case TOUCHWHEEL_MODE_LFO_DEPTH:
      snprintf(s_lfo_target_label, sizeof(s_lfo_target_label), "Target\n%s",
        touchwheel_lfo_target_label(scene->touchwheel_lfo_target));
      s_tw_items[item_count++] = (menu_item_t){
        s_lfo_target_label, nav_to_touchwheel_lfo_target, NULL, true, MENU_ITEM_KIND_ROLLER
      };
      break;

    default:
      break;
  }
  
  // Style roller (if mode supports it) (2-line format)
  if (mapping->supports_style_selection) {
    snprintf(s_style_label, sizeof(s_style_label), "Style\n%s", 
             style_to_string(scene->touchwheel_style));
    s_tw_items[item_count++] = (menu_item_t){s_style_label, nav_to_style, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  // Initial Value (endless CC or Double CC)
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS &&
      ((mapping->mode == TOUCHWHEEL_MODE_CONTINUOUS &&
        mapping->output_type == OUTPUT_TYPE_CC) ||
       mapping->mode == TOUCHWHEEL_MODE_DOUBLE_CC)) {
    snprintf(s_initial_value_label, sizeof(s_initial_value_label), 
      "Initial Value\n%u", (unsigned)scene->touchwheel_initial_value);
    s_tw_items[item_count++] = (menu_item_t){
      s_initial_value_label, nav_to_initial_value, NULL, true, MENU_ITEM_KIND_ROLLER
    };
  }
  
  return menu_create_page_2line("Touchwheel", s_tw_items, item_count);
}

// Cleanup function (call when leaving touchwheel menu)
void menu_page_touchwheel_cleanup(void) {
  free_cc_options();
}
