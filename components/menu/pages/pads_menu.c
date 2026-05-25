#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "scene.h"
#include "action.h"
#include "lfo.h"
#include "touchwheel_mode_mapping.h"
#include "tempo.h"
#include "ui.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_PADS"

// ============================================================================
// Forward Declarations
// ============================================================================

lv_obj_t* menu_page_pads_create(void);
static lv_obj_t* pad_detail_page_create(void);

// ============================================================================
// Static Storage with Double-Buffering
// ============================================================================
// We use double-buffering for label storage because menu_navigate_back_then_to
// uses async deletion. Old screens may still reference labels while new pages
// are being created. By alternating between two buffer sets, old screens always
// have valid label data.

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

// Main pads list
#define MAX_PAD_ITEMS 12
static menu_item_t s_pad_items[MAX_PAD_ITEMS];
static char s_pad_labels[LABEL_BUFFER_SETS][MAX_PAD_ITEMS][32];

// Pad detail page
#define MAX_DETAIL_ITEMS 12  // action + up to 8 slots + extra
static menu_item_t s_detail_items[MAX_DETAIL_ITEMS];
static char s_action_label[LABEL_BUFFER_SETS][48];
static char s_steps_label[LABEL_BUFFER_SETS][24];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];

// CC Hold slot submenu
static menu_item_t s_cc_hold_items[3];
static char s_cc_hold_cc_label[LABEL_BUFFER_SETS][40];
static char s_cc_hold_press_label[LABEL_BUFFER_SETS][40];
static char s_cc_hold_release_label[LABEL_BUFFER_SETS][40];

// CC Cycle slot submenu (CC + up to 8 steps)
#define MAX_CYCLE_STEPS 8
static menu_item_t s_cc_cycle_items[MAX_CYCLE_STEPS + 1];  // CC + 8 steps
static char s_cc_cycle_cc_label[LABEL_BUFFER_SETS][40];
static char s_cc_cycle_step_labels[LABEL_BUFFER_SETS][MAX_CYCLE_STEPS][40];

// Program Set
static char s_preset_label[LABEL_BUFFER_SETS][32];

// Preset Hold submenu
static menu_item_t s_preset_hold_items[2];
static char s_preset_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_preset_hold_release_label[LABEL_BUFFER_SETS][32];

// Preset Cycle
#define MAX_PRESET_CYCLE_STEPS 8
static char s_preset_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_preset_cycle_step_labels[LABEL_BUFFER_SETS][MAX_PRESET_CYCLE_STEPS][32];
static uint8_t s_editing_preset_step = 0;

// Tempo Hold submenu
static menu_item_t s_tempo_hold_items[2];
static char s_tempo_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_tempo_hold_release_label[LABEL_BUFFER_SETS][32];

// Tempo Cycle
#define MAX_TEMPO_CYCLE_STEPS 8
static char s_tempo_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_tempo_cycle_step_labels[LABEL_BUFFER_SETS][MAX_TEMPO_CYCLE_STEPS][32];
static uint8_t s_editing_tempo_step = 0;

// TW Hold submenu
static menu_item_t s_tw_hold_items[2];
static char s_tw_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_tw_hold_release_label[LABEL_BUFFER_SETS][32];

// TW Cycle
#define MAX_TW_CYCLE_STEPS 8
static char s_tw_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_tw_cycle_step_labels[LABEL_BUFFER_SETS][MAX_TW_CYCLE_STEPS][32];
static uint8_t s_editing_tw_step = 0;

// Scene Set
static char s_scene_label[LABEL_BUFFER_SETS][40];

// Set Tempo
static char s_tempo_label[LABEL_BUFFER_SETS][24];

// Note action
static char s_note_label[LABEL_BUFFER_SETS][24];

// Randomize action (8 CC slots)
#define MAX_RANDOMIZE_SLOTS 8
static char s_randomize_slot_labels[LABEL_BUFFER_SETS][MAX_RANDOMIZE_SLOTS][40];
static uint8_t s_editing_randomize_slot = 0;

// LFO action
static char s_lfo_slot_label[LABEL_BUFFER_SETS][24];

// UI module actions
static char s_ui_module_label[LABEL_BUFFER_SETS][32];
static char s_ui_module2_label[LABEL_BUFFER_SETS][32];

// UI Cycle
#define MAX_UI_CYCLE_STEPS 8
static char s_ui_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_ui_cycle_step_labels[LABEL_BUFFER_SETS][MAX_UI_CYCLE_STEPS][32];
static uint8_t s_editing_ui_step = 0;

// Param slot actions
static char s_param_label[LABEL_BUFFER_SETS][32];
static char s_param2_label[LABEL_BUFFER_SETS][32];
#define MAX_PARAM_CYCLE_STEPS 8
static char s_param_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_param_cycle_step_labels[LABEL_BUFFER_SETS][MAX_PARAM_CYCLE_STEPS][32];
static uint8_t s_editing_param_step = 0;

// Filtered CC options for randomize roller (excludes already-selected CCs)
typedef struct {
  char* options_str;      // Filtered options string for roller
  uint8_t* cc_numbers;    // Filtered CC number mapping
  uint16_t count;         // Number of filtered options
} filtered_cc_options_t;

static filtered_cc_options_t s_randomize_cc_options = {0};
static filtered_cc_options_t s_param_cc_options = {0};

// Note name lookup table
static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Get current label buffer set and advance to next
static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

// Editing state
static uint8_t s_editing_pad_index = 0;
static uint8_t s_editing_cc_slot = 0;
static uint8_t s_editing_step = 0;  // For CC Cycle step editing
static uint8_t s_pending_cc_number = 0;
static const midi_control_t* s_pending_control = NULL;  // For value roller

// Action config context for pad actions
static action_config_context_t s_pad_action_ctx;

// Re-entry guard
static bool s_callback_in_progress = false;

// Custom back handler for pad detail page - recreates Pads list when going back
static bool pad_detail_handle_back(void) {
  menu_set_custom_back_handler(NULL);  // Clear handler before navigation
  // Pop 2: pad detail AND old Pads, then push fresh Pads at same depth
  menu_navigate_back_then_to(2, "Pads", menu_page_pads_create);
  return true;  // Handled, don't do normal back
}

// Dynamic CC options (loaded from device)
typedef struct {
  char* options_str;       // Newline-separated options for roller
  uint8_t* cc_numbers;     // CC numbers corresponding to each option
  uint16_t count;          // Number of CC options (including Inactive)
} cc_options_t;

static cc_options_t s_cc_options = {0};

// ============================================================================
// Pad Naming
// ============================================================================

static const char* get_pad_display_name(uint8_t index) {
  static const char* names[] = {
    "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Pad 7", "Pad 8",  // Touchwheel segments
    "Omega",   // Pad 8 - main activation
    "Alpha",   // Pad 9
    "Beta",    // Pad 10
    "Gamma"    // Pad 11
  };
  if (index < 12) return names[index];
  return "?";
}

// ============================================================================
// NOTE: The local action-type picker has been removed from this file.
// The live picker for pads lives in components/menu/pages/action_config.c;
// pads enter that flow via nav_to_pad_detail() -> action_config_start().
// A dead pad_detail_page_create chain remains below (out of scope for
// this pass); nav_to_action_type is kept as an empty stub to preserve
// compile.
// ============================================================================

// Thin wrapper around action_type_to_string that uses "<None>" for the
// "unassigned" slot in the picker UI (the canonical name is just "None").
// New action types only need an entry in action_strings.c's table.
static const char* get_action_display_name(action_type_t type) {
  if (type == ACTION_NONE) return "<None>";
  return action_type_to_string(type);
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
  
  // Estimate string size: avg 24 chars per name + newlines
  size_t str_size = total * 28;
  s_cc_options.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_cc_options.cc_numbers || !s_cc_options.options_str) {
    ESP_LOGE(TAG, "Failed to allocate CC options");
    free_cc_options();
    return false;
  }
  
  // Build options string: "Inactive\nName1\nName2..."
  strcpy(s_cc_options.options_str, "Inactive");
  s_cc_options.cc_numbers[0] = 0xFF;  // Sentinel for "Inactive"
  s_cc_options.count = 1;
  
  size_t pos = strlen("Inactive");
  for (uint16_t i = 0; i < device->control_count && (pos + 28) < str_size; i++) {
    const midi_control_t* ctrl = &device->controls[i];
    if (ctrl->type == MIDI_CONTROL_TYPE_CC) {
      s_cc_options.options_str[pos++] = '\n';
      const char* name = ctrl->name ? ctrl->name : "Unknown";
      size_t name_len = strlen(name);
      if (name_len > 24) name_len = 24;  // Truncate long names
      memcpy(s_cc_options.options_str + pos, name, name_len);
      pos += name_len;
      s_cc_options.options_str[pos] = '\0';
      
      s_cc_options.cc_numbers[s_cc_options.count] = (uint8_t)ctrl->id;
      s_cc_options.count++;
    }
  }
  
  ESP_LOGD(TAG, "Loaded %u CC options from device", (unsigned)s_cc_options.count);
  return true;
}

// Find option index for a CC number (returns 0 for not found/Inactive)
static uint32_t cc_number_to_option_index(uint8_t cc_num) {
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    if (s_cc_options.cc_numbers[i] == cc_num) {
      return i;
    }
  }
  return 0;  // Not found defaults to Inactive
}

// ============================================================================
// Helper Functions
// ============================================================================

static void persist_scene_changes(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = scene_get_current_index();
    scene_save_to_flash(scene_index);
  }
}

static void format_pad_label(uint8_t index, action_t* action, char* buf, size_t buf_size) {
  const char* name = get_pad_display_name(index);
  if (action && action->type != ACTION_NONE) {
    const char* action_name = get_action_display_name(action->type);
    snprintf(buf, buf_size, "%s\n%s", name, action_name);
  } else {
    snprintf(buf, buf_size, "%s\n<None>", name);
  }
}

// Get display name for a CC slot: "ControlName: ValueName" or "ControlName: 64"
static const char* get_cc_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  
  // Slot is inactive if it's beyond num_ccs (consecutive slots from 0)
  if (slot >= action->params.control.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  uint8_t cc_val = action->params.control.values[slot];
  
  // Get device and control info
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char buf[40];
  
  // Get control name
  const char* ctrl_name = (ctrl && ctrl->name) ? ctrl->name : NULL;
  
  // Check for discrete value name (function handles lookup internally)
  const char* value_name = assets_get_discrete_name(device, cc_num, cc_val);
  
  if (ctrl_name) {
    if (value_name) {
      // "Filter: High Pass"
      snprintf(buf, sizeof(buf), "%.16s: %.18s", ctrl_name, value_name);
    } else {
      // "Filter: 64"
      snprintf(buf, sizeof(buf), "%.24s: %u", ctrl_name, (unsigned)cc_val);
    }
  } else {
    // Fallback: "CC21: 64"
    snprintf(buf, sizeof(buf), "CC%u: %u", (unsigned)cc_num, (unsigned)cc_val);
  }
  return buf;
}

// Get display name for a CC Hold slot: just the CC name
static const char* get_cc_hold_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  
  // Slot is inactive if it's beyond num_ccs
  if (slot >= action->params.control.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  
  // Get device and control info
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char buf[32];
  
  if (ctrl && ctrl->name) {
    snprintf(buf, sizeof(buf), "%.28s", ctrl->name);
  } else {
    snprintf(buf, sizeof(buf), "CC %u", (unsigned)cc_num);
  }
  return buf;
}

// Get value display string for CC Hold/Cycle submenu items
static const char* get_value_display(const device_def_t* device, uint8_t cc_num, uint8_t value) {
  const char* discrete_name = assets_get_discrete_name(device, cc_num, value);
  static char buf[24];
  if (discrete_name) {
    snprintf(buf, sizeof(buf), "%.20s", discrete_name);
  } else {
    snprintf(buf, sizeof(buf), "%u", (unsigned)value);
  }
  return buf;
}

// Get display name for a CC Cycle slot: just the CC name
static const char* get_cc_cycle_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  
  // Slot is inactive if it's beyond num_ccs
  if (slot >= action->params.control.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  
  // Get device and control info
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char buf[32];
  
  if (ctrl && ctrl->name) {
    snprintf(buf, sizeof(buf), "%.28s", ctrl->name);
  } else {
    snprintf(buf, sizeof(buf), "CC %u", (unsigned)cc_num);
  }
  return buf;
}

// ============================================================================
// Navigation Callbacks
// ============================================================================

static void nav_to_pad_detail(void* user_data) {
  s_editing_pad_index = (uint8_t)(uintptr_t)user_data;
  
  // Get the pad's action for configuration
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    ESP_LOGW(TAG, "No mapping for pad %u", (unsigned)s_editing_pad_index);
    return;
  }
  
  // Set up action config context
  const char* title = get_pad_display_name(s_editing_pad_index);
  s_pad_action_ctx.target_action = &mapping->action;
  s_pad_action_ctx.source_title = "Pads";
  s_pad_action_ctx.detail_title = title;
  s_pad_action_ctx.return_page = menu_page_pads_create;
  s_pad_action_ctx.return_depth = 2;  // Pop detail + old Pads, push fresh Pads
  s_pad_action_ctx.on_complete = NULL;
  s_pad_action_ctx.user_data = NULL;
  s_pad_action_ctx.trigger_type = (s_editing_pad_index <= 7) ?
    ACTION_TRIGGER_TOUCHPAD_0_7 : ACTION_TRIGGER_TOUCHPAD_8_11;
  
  action_config_start(&s_pad_action_ctx);
}

// ============================================================================
// Action Type Roller -- DEAD STUB
//
// The live picker lives in components/menu/pages/action_config.c. This
// stub exists only because the dead pad_detail_page_create chain below
// still references nav_to_action_type. The chain itself is unreachable
// (the only entry point, nav_to_pad_detail, jumps straight to
// action_config_start), so the body is intentionally empty.
// ============================================================================

static void nav_to_action_type(void* user_data) {
  (void)user_data;
}

// ============================================================================
// CC Value Roller (second step - value selection)
// ============================================================================

static void cc_value_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_value;
  
  // Determine the actual value based on control type
  if (s_pending_control && s_pending_control->discrete_count > 0) {
    // Discrete values - index maps to discrete_values array
    if (selected_index < s_pending_control->discrete_count) {
      cc_value = (uint8_t)s_pending_control->discrete_values[selected_index].value;
    } else {
      cc_value = 0;
    }
  } else if (s_pending_control) {
    // Range values - index + min
    cc_value = (uint8_t)(s_pending_control->min + selected_index);
  } else {
    // Fallback: direct 0-127
    cc_value = (uint8_t)selected_index;
  }
  
  // Save CC number and value to the slot
  mapping->action.params.control.cc_numbers[slot] = s_pending_cc_number;
  mapping->action.params.control.values[slot] = cc_value;
  
  // If this slot was inactive (slot >= num_ccs), we're adding a new CC
  // New CCs always go at position num_ccs, so just increment
  if (slot >= mapping->action.params.control.num_ccs) {
    mapping->action.params.control.num_ccs = slot + 1;
  }
  
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC slot %u set to CC%u=%u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1),
    (unsigned)s_pending_cc_number, (unsigned)cc_value);
  
  s_pending_control = NULL;
  s_callback_in_progress = false;
  
  // Go back 3: pop CC value roller, CC number roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(3, title, pad_detail_page_create);
}

static lv_obj_t* cc_value_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = s_editing_cc_slot;
  
  // Get device and control info
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  s_pending_control = assets_get_control_by_cc(device, s_pending_cc_number);
  
  static char options[1024];
  options[0] = '\0';
  uint32_t current_idx = 0;
  uint8_t current_val = mapping->action.params.control.values[slot];
  
  if (s_pending_control && s_pending_control->discrete_count > 0) {
    // Discrete values - show names
    for (int i = 0; i < s_pending_control->discrete_count; i++) {
      if (i > 0) strcat(options, "\n");
      const char* name = s_pending_control->discrete_values[i].name;
      if (name) {
        // Truncate long names
        char truncated[28];
        strncpy(truncated, name, 27);
        truncated[27] = '\0';
        strcat(options, truncated);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", 
          (unsigned)s_pending_control->discrete_values[i].value);
        strcat(options, num);
      }
      
      // Find current value's index
      if (s_pending_control->discrete_values[i].value == current_val) {
        current_idx = (uint32_t)i;
      }
    }
  } else if (s_pending_control) {
    // Range values - show min to max
    uint16_t min_val = s_pending_control->min;
    uint16_t max_val = s_pending_control->max;
    if (max_val > 127) max_val = 127;
    
    for (uint16_t i = min_val; i <= max_val; i++) {
      if (i > min_val) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%u", (unsigned)i);
      strcat(options, num);
    }
    
    if (current_val >= min_val && current_val <= max_val) {
      current_idx = current_val - min_val;
    }
  } else {
    // Fallback: 0-127
    for (int i = 0; i <= 127; i++) {
      if (i > 0) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%d", i);
      strcat(options, num);
    }
    current_idx = current_val;
  }
  
  // Get control name for title
  const char* ctrl_name = s_pending_control ? s_pending_control->name : NULL;
  static char title[32];
  if (ctrl_name) {
    snprintf(title, sizeof(title), "%.20s Value", ctrl_name);
  } else {
    snprintf(title, sizeof(title), "CC%u Value", (unsigned)s_pending_cc_number);
  }
  
  return menu_create_roller_page(title, options, current_idx, cc_value_confirm_cb, NULL);
}

// ============================================================================
// CC Number Roller (first step - control selection)
// ============================================================================

static void cc_number_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact remaining
    scene_t* scene = scene_get_current();
    if (scene) {
      touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
        scene_get_current_index(), s_editing_pad_index);
      if (mapping) {
        uint8_t slot = s_editing_cc_slot;
        uint8_t num_ccs = mapping->action.params.control.num_ccs;
        
        // Only clear if this slot is actually active
        if (slot < num_ccs) {
          // Compact: shift slots after this one down
          for (int i = slot; i < 3; i++) {
            mapping->action.params.control.cc_numbers[i] = mapping->action.params.control.cc_numbers[i + 1];
            mapping->action.params.control.values[i] = mapping->action.params.control.values[i + 1];
            mapping->action.params.control.values2[i] = mapping->action.params.control.values2[i + 1];
          }
          // Clear the last slot
          mapping->action.params.control.cc_numbers[3] = 0;
          mapping->action.params.control.values[3] = 0;
          mapping->action.params.control.values2[3] = 0;
          
          // Decrement num_ccs
          if (num_ccs > 0) {
            mapping->action.params.control.num_ccs = num_ccs - 1;
          }
          
          persist_scene_changes();
          ESP_LOGI(TAG, "Pad %s CC slot %u cleared (compacted to %u)",
            get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1),
            (unsigned)mapping->action.params.control.num_ccs);
        }
      }
    }
    
    s_callback_in_progress = false;
    
    // Go back 2: pop CC number roller, old pad detail, push fresh pad detail
    const char* title = get_pad_display_name(s_editing_pad_index);
    menu_navigate_back_then_to(2, title, pad_detail_page_create);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    s_pending_cc_number = s_cc_options.cc_numbers[selected_index];
  } else {
    s_pending_cc_number = 0;
  }
  
  s_callback_in_progress = false;
  
  // Navigate to value roller
  menu_navigate_to("Value", cc_value_roller_create);
}

static lv_obj_t* cc_number_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Ensure CC options are loaded
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      // Fallback: build simple 0-127 list
      ESP_LOGW(TAG, "No device CC options, using raw numbers");
      
      // This shouldn't happen often, but provide a fallback
      static char fallback_options[768];
      strcpy(fallback_options, "Inactive");
      for (int i = 0; i <= 127; i++) {
        char num[8];
        snprintf(num, sizeof(num), "\n%d", i);
        strcat(fallback_options, num);
      }
      
      uint8_t slot = s_editing_cc_slot;
      uint32_t current_idx = 0;  // Default to "Inactive"
      
      // Only look up CC if this slot is active
      if (slot < mapping->action.params.control.num_ccs) {
        uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
        current_idx = cc_num + 1;  // +1 for "Inactive" at index 0
      }
      
      return menu_create_roller_page("CC Number", fallback_options, current_idx, 
        cc_number_confirm_cb, NULL);
    }
  }
  
  // Get current CC number for this slot
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;  // Default to "Inactive"
  
  // Only look up CC if this slot is active
  if (slot < mapping->action.params.control.num_ccs) {
    uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
    current_idx = cc_number_to_option_index(cc_num);
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(slot + 1));
  
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx, 
    cc_number_confirm_cb, NULL);
}

static void nav_to_cc_slot(void* user_data);  // Forward declaration

// ============================================================================
// CC Hold Slot Submenu and Rollers
// ============================================================================

static lv_obj_t* cc_hold_slot_page_create(void);  // Forward declaration

// --- CC Hold: CC Number Roller ---

static void cc_hold_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact
    uint8_t num_ccs = mapping->action.params.control.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        mapping->action.params.control.cc_numbers[i] = mapping->action.params.control.cc_numbers[i + 1];
        mapping->action.params.control.values[i] = mapping->action.params.control.values[i + 1];
        mapping->action.params.control.values2[i] = mapping->action.params.control.values2[i + 1];
      }
      mapping->action.params.control.cc_numbers[3] = 0;
      mapping->action.params.control.values[3] = 0;
      mapping->action.params.control.values2[3] = 0;
      if (num_ccs > 0) {
        mapping->action.params.control.num_ccs = num_ccs - 1;
      }
      persist_scene_changes();
      ESP_LOGI(TAG, "Pad %s CC Hold slot %u cleared",
        get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    // Go back 3: pop CC roller, slot submenu, old pad detail, push fresh pad detail
    const char* title = get_pad_display_name(s_editing_pad_index);
    menu_navigate_back_then_to(3, title, pad_detail_page_create);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    mapping->action.params.control.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= mapping->action.params.control.num_ccs) {
      mapping->action.params.control.values[slot] = 127;   // Default press value
      mapping->action.params.control.values2[slot] = 0;    // Default release value
      mapping->action.params.control.num_ccs = slot + 1;
    }
    
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %s CC Hold slot %u CC set to %u",
      get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Go back 3: pop CC roller, slot submenu, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(3, title, pad_detail_page_create);
}

static lv_obj_t* cc_hold_cc_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      return menu_create_page("Error", NULL, 0);
    }
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < mapping->action.params.control.num_ccs) {
    uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
    current_idx = cc_number_to_option_index(cc_num);
  }
  
  return menu_create_roller_page("CC", s_cc_options.options_str, current_idx,
    cc_hold_cc_confirm_cb, NULL);
}

static void nav_to_cc_hold_cc(void* user_data) {
  (void)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  menu_navigate_to("CC", cc_hold_cc_roller_create);
}

// --- CC Hold: Press Value Roller ---

static void cc_hold_press_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  
  // Get device and control to determine actual value
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  uint8_t press_val;
  if (ctrl && ctrl->discrete_count > 0) {
    if (selected_index < ctrl->discrete_count) {
      press_val = (uint8_t)ctrl->discrete_values[selected_index].value;
    } else {
      press_val = 0;
    }
  } else if (ctrl) {
    press_val = (uint8_t)(ctrl->min + selected_index);
  } else {
    press_val = (uint8_t)selected_index;
  }
  
  mapping->action.params.control.values[slot] = press_val;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC Hold slot %u press value set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)press_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_hold_slot_page_create);
}

static lv_obj_t* cc_hold_press_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.control.values[slot];
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char options[1024];
  options[0] = '\0';
  uint32_t current_idx = 0;
  
  if (ctrl && ctrl->discrete_count > 0) {
    for (int i = 0; i < ctrl->discrete_count; i++) {
      if (i > 0) strcat(options, "\n");
      const char* name = ctrl->discrete_values[i].name;
      if (name) {
        char truncated[28];
        strncpy(truncated, name, 27);
        truncated[27] = '\0';
        strcat(options, truncated);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)ctrl->discrete_values[i].value);
        strcat(options, num);
      }
      if (ctrl->discrete_values[i].value == current_val) {
        current_idx = (uint32_t)i;
      }
    }
  } else if (ctrl) {
    uint16_t min_val = ctrl->min;
    uint16_t max_val = ctrl->max > 127 ? 127 : ctrl->max;
    for (uint16_t i = min_val; i <= max_val; i++) {
      if (i > min_val) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%u", (unsigned)i);
      strcat(options, num);
    }
    if (current_val >= min_val && current_val <= max_val) {
      current_idx = current_val - min_val;
    }
  } else {
    for (int i = 0; i <= 127; i++) {
      if (i > 0) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%d", i);
      strcat(options, num);
    }
    current_idx = current_val;
  }
  
  return menu_create_roller_page("Press", options, current_idx, cc_hold_press_confirm_cb, NULL);
}

static void nav_to_cc_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", cc_hold_press_roller_create);
}

// --- CC Hold: Release Value Roller ---

static void cc_hold_release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  uint8_t release_val;
  if (ctrl && ctrl->discrete_count > 0) {
    if (selected_index < ctrl->discrete_count) {
      release_val = (uint8_t)ctrl->discrete_values[selected_index].value;
    } else {
      release_val = 0;
    }
  } else if (ctrl) {
    release_val = (uint8_t)(ctrl->min + selected_index);
  } else {
    release_val = (uint8_t)selected_index;
  }
  
  mapping->action.params.control.values2[slot] = release_val;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC Hold slot %u release value set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)release_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_hold_slot_page_create);
}

static lv_obj_t* cc_hold_release_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.control.values2[slot];
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char options[1024];
  options[0] = '\0';
  uint32_t current_idx = 0;
  
  if (ctrl && ctrl->discrete_count > 0) {
    for (int i = 0; i < ctrl->discrete_count; i++) {
      if (i > 0) strcat(options, "\n");
      const char* name = ctrl->discrete_values[i].name;
      if (name) {
        char truncated[28];
        strncpy(truncated, name, 27);
        truncated[27] = '\0';
        strcat(options, truncated);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)ctrl->discrete_values[i].value);
        strcat(options, num);
      }
      if (ctrl->discrete_values[i].value == current_val) {
        current_idx = (uint32_t)i;
      }
    }
  } else if (ctrl) {
    uint16_t min_val = ctrl->min;
    uint16_t max_val = ctrl->max > 127 ? 127 : ctrl->max;
    for (uint16_t i = min_val; i <= max_val; i++) {
      if (i > min_val) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%u", (unsigned)i);
      strcat(options, num);
    }
    if (current_val >= min_val && current_val <= max_val) {
      current_idx = current_val - min_val;
    }
  } else {
    for (int i = 0; i <= 127; i++) {
      if (i > 0) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%d", i);
      strcat(options, num);
    }
    current_idx = current_val;
  }
  
  return menu_create_roller_page("Release", options, current_idx, cc_hold_release_confirm_cb, NULL);
}

static void nav_to_cc_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", cc_hold_release_roller_create);
}

// --- CC Hold Slot Submenu Page ---

static lv_obj_t* cc_hold_slot_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return menu_create_page("Error", NULL, 0);
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return menu_create_page("Error", NULL, 0);
  
  int buf = get_next_buffer_set();
  uint8_t slot = s_editing_cc_slot;
  
  // Get current values
  bool is_active = (slot < mapping->action.params.control.num_ccs);
  uint8_t cc_num = is_active ? mapping->action.params.control.cc_numbers[slot] : 0;
  uint8_t press_val = is_active ? mapping->action.params.control.values[slot] : 0;
  uint8_t release_val = is_active ? mapping->action.params.control.values2[slot] : 0;
  
  // Get device for control names
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  // Build labels
  if (is_active) {
    const char* cc_name = assets_get_cc_name(device, cc_num);
    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(s_cc_hold_cc_label[buf], sizeof(s_cc_hold_cc_label[buf]), "CC: %.30s", cc_name);
    } else {
      snprintf(s_cc_hold_cc_label[buf], sizeof(s_cc_hold_cc_label[buf]), "CC: %u", (unsigned)cc_num);
    }
    
    const char* press_disp = get_value_display(device, cc_num, press_val);
    snprintf(s_cc_hold_press_label[buf], sizeof(s_cc_hold_press_label[buf]), "Press: %s", press_disp);
    
    const char* release_disp = get_value_display(device, cc_num, release_val);
    snprintf(s_cc_hold_release_label[buf], sizeof(s_cc_hold_release_label[buf]), "Release: %s", release_disp);
  } else {
    snprintf(s_cc_hold_cc_label[buf], sizeof(s_cc_hold_cc_label[buf]), "CC: <none>");
    snprintf(s_cc_hold_press_label[buf], sizeof(s_cc_hold_press_label[buf]), "Press: <none>");
    snprintf(s_cc_hold_release_label[buf], sizeof(s_cc_hold_release_label[buf]), "Release: <none>");
  }
  
  s_cc_hold_items[0] = (menu_item_t){s_cc_hold_cc_label[buf], nav_to_cc_hold_cc, NULL, true};
  s_cc_hold_items[1] = (menu_item_t){s_cc_hold_press_label[buf], nav_to_cc_hold_press, NULL, is_active};
  s_cc_hold_items[2] = (menu_item_t){s_cc_hold_release_label[buf], nav_to_cc_hold_release, NULL, is_active};
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  
  return menu_create_page(title, s_cc_hold_items, 3);
}

// ============================================================================
// CC Cycle Slot Submenu and Rollers
// ============================================================================

static lv_obj_t* cc_cycle_slot_page_create(void);  // Forward declaration

// --- CC Cycle: CC Number Roller ---

static void cc_cycle_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact
    uint8_t num_ccs = mapping->action.params.control.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        mapping->action.params.control.cc_numbers[i] = mapping->action.params.control.cc_numbers[i + 1];
        for (int s = 0; s < MAX_CYCLE_STEPS; s++) {
          mapping->action.params.control.cycle_values[i][s] = 
            mapping->action.params.control.cycle_values[i + 1][s];
        }
      }
      mapping->action.params.control.cc_numbers[3] = 0;
      memset(mapping->action.params.control.cycle_values[3], 0, MAX_CYCLE_STEPS);
      if (num_ccs > 0) {
        mapping->action.params.control.num_ccs = num_ccs - 1;
      }
      persist_scene_changes();
      ESP_LOGI(TAG, "Pad %s CC Cycle slot %u cleared",
        get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    // Go back 3: pop CC roller, slot submenu, old pad detail, push fresh pad detail
    const char* title = get_pad_display_name(s_editing_pad_index);
    menu_navigate_back_then_to(3, title, pad_detail_page_create);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    mapping->action.params.control.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= mapping->action.params.control.num_ccs) {
      // Default cycle values: 0, 127 for 2 steps
      uint8_t steps = mapping->action.params.control.num_cycle_steps;
      if (steps < 2) {
        mapping->action.params.control.num_cycle_steps = 2;
        steps = 2;
      }
      mapping->action.params.control.cycle_values[slot][0] = 0;
      mapping->action.params.control.cycle_values[slot][1] = 127;
      mapping->action.params.control.num_ccs = slot + 1;
    }
    
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %s CC Cycle slot %u CC set to %u",
      get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Go back 3: pop CC roller, slot submenu, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(3, title, pad_detail_page_create);
}

static lv_obj_t* cc_cycle_cc_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      return menu_create_page("Error", NULL, 0);
    }
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < mapping->action.params.control.num_ccs) {
    uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
    current_idx = cc_number_to_option_index(cc_num);
  }
  
  return menu_create_roller_page("CC", s_cc_options.options_str, current_idx,
    cc_cycle_cc_confirm_cb, NULL);
}

static void nav_to_cc_cycle_cc(void* user_data) {
  (void)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  menu_navigate_to("CC", cc_cycle_cc_roller_create);
}

// --- CC Cycle: Step Value Roller ---

static void cc_cycle_step_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t step = s_editing_step;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  
  // Get device and control to determine actual value
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  uint8_t step_val;
  if (ctrl && ctrl->discrete_count > 0) {
    if (selected_index < ctrl->discrete_count) {
      step_val = (uint8_t)ctrl->discrete_values[selected_index].value;
    } else {
      step_val = 0;
    }
  } else if (ctrl) {
    step_val = (uint8_t)(ctrl->min + selected_index);
  } else {
    step_val = (uint8_t)selected_index;
  }
  
  mapping->action.params.control.cycle_values[slot][step] = step_val;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC Cycle slot %u step %u value set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), 
    (unsigned)(step + 1), (unsigned)step_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_cycle_slot_page_create);
}

static lv_obj_t* cc_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = s_editing_cc_slot;
  uint8_t step = s_editing_step;
  uint8_t cc_num = mapping->action.params.control.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.control.cycle_values[slot][step];
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char options[1024];
  options[0] = '\0';
  uint32_t current_idx = 0;
  
  if (ctrl && ctrl->discrete_count > 0) {
    for (int i = 0; i < ctrl->discrete_count; i++) {
      if (i > 0) strcat(options, "\n");
      const char* name = ctrl->discrete_values[i].name;
      if (name) {
        char truncated[28];
        strncpy(truncated, name, 27);
        truncated[27] = '\0';
        strcat(options, truncated);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)ctrl->discrete_values[i].value);
        strcat(options, num);
      }
      if (ctrl->discrete_values[i].value == current_val) {
        current_idx = (uint32_t)i;
      }
    }
  } else if (ctrl) {
    uint16_t min_val = ctrl->min;
    uint16_t max_val = ctrl->max > 127 ? 127 : ctrl->max;
    for (uint16_t i = min_val; i <= max_val; i++) {
      if (i > min_val) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%u", (unsigned)i);
      strcat(options, num);
    }
    if (current_val >= min_val && current_val <= max_val) {
      current_idx = current_val - min_val;
    }
  } else {
    for (int i = 0; i <= 127; i++) {
      if (i > 0) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%d", i);
      strcat(options, num);
    }
    current_idx = current_val;
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  return menu_create_roller_page(title, options, current_idx, cc_cycle_step_confirm_cb, NULL);
}

static void nav_to_cc_cycle_step(void* user_data) {
  s_editing_step = (uint8_t)(uintptr_t)user_data;
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_step + 1));
  menu_navigate_to(title, cc_cycle_step_roller_create);
}

// --- CC Cycle Slot Submenu Page ---

static lv_obj_t* cc_cycle_slot_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return menu_create_page("Error", NULL, 0);
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return menu_create_page("Error", NULL, 0);
  
  int buf = get_next_buffer_set();
  uint8_t slot = s_editing_cc_slot;
  
  // Get current values
  bool is_active = (slot < mapping->action.params.control.num_ccs);
  uint8_t cc_num = is_active ? mapping->action.params.control.cc_numbers[slot] : 0;
  uint8_t num_steps = mapping->action.params.control.num_cycle_steps;
  if (num_steps < 2) num_steps = 2;
  
  // Get device for control names
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  int item_count = 0;
  
  // CC selector
  if (is_active) {
    const char* cc_name = assets_get_cc_name(device, cc_num);
    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(s_cc_cycle_cc_label[buf], sizeof(s_cc_cycle_cc_label[buf]), "CC: %.30s", cc_name);
    } else {
      snprintf(s_cc_cycle_cc_label[buf], sizeof(s_cc_cycle_cc_label[buf]), "CC: %u", (unsigned)cc_num);
    }
  } else {
    snprintf(s_cc_cycle_cc_label[buf], sizeof(s_cc_cycle_cc_label[buf]), "CC: <none>");
  }
  s_cc_cycle_items[item_count++] = (menu_item_t){s_cc_cycle_cc_label[buf], nav_to_cc_cycle_cc, NULL, true};
  
  // Step value items (only if slot is active)
  if (is_active) {
    for (int i = 0; i < num_steps && i < MAX_CYCLE_STEPS; i++) {
      uint8_t step_val = mapping->action.params.control.cycle_values[slot][i];
      const char* val_disp = get_value_display(device, cc_num, step_val);
      snprintf(s_cc_cycle_step_labels[buf][i], sizeof(s_cc_cycle_step_labels[buf][i]), 
        "Step %d: %s", i + 1, val_disp);
      s_cc_cycle_items[item_count++] = (menu_item_t){
        s_cc_cycle_step_labels[buf][i], nav_to_cc_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  
  return menu_create_page(title, s_cc_cycle_items, item_count);
}

// ============================================================================
// CC Cycle Steps Roller (on pad detail page)
// ============================================================================

static void cc_cycle_steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Steps roller shows 2-8, so index 0 = 2 steps
  uint8_t new_steps = (uint8_t)(selected_index + 2);
  mapping->action.params.control.num_cycle_steps = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC Cycle steps set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)new_steps);
  
  s_callback_in_progress = false;
  
  // Go back 2: pop steps roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* cc_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build options: 2, 3, 4, 5, 6, 7, 8
  static char options[32];
  snprintf(options, sizeof(options), "2\n3\n4\n5\n6\n7\n8");
  
  uint8_t current_steps = mapping->action.params.control.num_cycle_steps;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;  // 2 steps = index 0
  
  return menu_create_roller_page("Steps", options, current_idx, 
    cc_cycle_steps_confirm_cb, NULL);
}

static void nav_to_cc_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", cc_cycle_steps_roller_create);
}

// ============================================================================
// Program Set Roller
// ============================================================================

static void program_set_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Get device to check index_base
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  // Roller shows 1 to count, but internal value is (selected_index + index_base)
  uint16_t program = (uint16_t)(selected_index + index_base);
  mapping->action.params.preset.program = program;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s Program Set preset set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  
  // Go back 2: pop preset roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* program_set_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Get device for preset count
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t count = 128;  // Default
  uint16_t index_base = 0;
  if (device && device->pc_info) {
    count = device->pc_info->count;
    index_base = device->pc_info->index_base;
  }
  
  // Build options: 1, 2, 3, ... count
  // Allocate enough for "128\n" * 128 entries = ~640 bytes
  static char options[1024];
  options[0] = '\0';
  char* pos = options;
  size_t remaining = sizeof(options);
  
  for (uint16_t i = 1; i <= count && remaining > 8; i++) {
    int written = snprintf(pos, remaining, "%s%u", i > 1 ? "\n" : "", (unsigned)i);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  // Current selection: convert internal value to roller index
  uint16_t current_program = mapping->action.params.preset.program;
  uint32_t current_idx = 0;
  if (current_program >= index_base) {
    current_idx = current_program - index_base;
    if (current_idx >= count) current_idx = 0;
  }
  
  return menu_create_roller_page("Preset", options, current_idx, 
    program_set_confirm_cb, NULL);
}

static void nav_to_program_set(void* user_data) {
  (void)user_data;
  menu_navigate_to("Preset", program_set_roller_create);
}

// ============================================================================
// Scene Set Roller
// ============================================================================

static void scene_set_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Get scene index from manifest position
  uint8_t scene_index = scene_get_index_by_position((uint16_t)selected_index);
  mapping->action.params.target.number = scene_index;
  persist_scene_changes();
  
  const char* scene_name = scene_get_name_by_position((uint16_t)selected_index);
  ESP_LOGI(TAG, "Pad %s Scene Set target set to %s (index %u)",
    get_pad_display_name(s_editing_pad_index), 
    scene_name ? scene_name : "?", (unsigned)scene_index);
  
  s_callback_in_progress = false;
  
  // Go back 2: pop scene roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* scene_set_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint16_t count = scene_get_total_count();
  if (count == 0) {
    return menu_create_page("No Scenes", NULL, 0);
  }
  
  // Build options from scene names
  static char options[1024];
  options[0] = '\0';
  char* pos = options;
  size_t remaining = sizeof(options);
  
  uint32_t current_idx = 0;
  uint8_t target_scene = mapping->action.params.target.number;
  
  for (uint16_t i = 0; i < count && remaining > 40; i++) {
    const char* name = scene_get_name_by_position(i);
    if (!name) name = "?";
    
    int written = snprintf(pos, remaining, "%s%s", i > 0 ? "\n" : "", name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
    
    // Find current selection
    if (scene_get_index_by_position(i) == target_scene) {
      current_idx = i;
    }
  }
  
  return menu_create_roller_page("Scene", options, current_idx, 
    scene_set_confirm_cb, NULL);
}

static void nav_to_scene_set(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene", scene_set_roller_create);
}

// ============================================================================
// Set Tempo Roller
// ============================================================================

static void set_tempo_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Roller shows 20-300, so index 0 = 20 BPM
  uint16_t bpm = (uint16_t)(selected_index + 20);
  mapping->action.params.tempo.bpm = bpm;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s Set Tempo set to %u BPM",
    get_pad_display_name(s_editing_pad_index), (unsigned)bpm);
  
  s_callback_in_progress = false;
  
  // Go back 2: pop tempo roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* set_tempo_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build options: 20, 21, 22, ... 300
  static char options[2048];
  options[0] = '\0';
  char* pos = options;
  size_t remaining = sizeof(options);
  
  for (uint16_t bpm = 20; bpm <= 300 && remaining > 8; bpm++) {
    int written = snprintf(pos, remaining, "%s%u", bpm > 20 ? "\n" : "", (unsigned)bpm);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  // Current selection: convert BPM to roller index
  uint16_t current_bpm = mapping->action.params.tempo.bpm;
  if (current_bpm < 20) current_bpm = 120;  // Default
  if (current_bpm > 300) current_bpm = 300;
  uint32_t current_idx = current_bpm - 20;
  
  return menu_create_roller_page("Tempo", options, current_idx, 
    set_tempo_confirm_cb, NULL);
}

static void nav_to_set_tempo(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tempo", set_tempo_roller_create);
}

// ============================================================================
// Note Roller (for ACTION_NOTE)
// ============================================================================

// Helper to get note display name from MIDI number
static void get_note_display_name(uint8_t midi_note, char* buf, size_t buf_size) {
  int octave = (midi_note / 12) - 1;  // C4 (60) = octave 4
  int note_idx = midi_note % 12;
  snprintf(buf, buf_size, "%s%d", NOTE_NAMES[note_idx], octave);
}

static void note_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  // Convert roller index to MIDI note (C2=36 is index 0)
  uint8_t midi_note = 36 + selected_idx;
  if (midi_note > 96) midi_note = 96;
  
  mapping->action.params.note.note = midi_note;
  persist_scene_changes();
  
  char note_name[8];
  get_note_display_name(midi_note, note_name, sizeof(note_name));
  ESP_LOGI(TAG, "Pad %s note set to %s (MIDI %u)",
    get_pad_display_name(s_editing_pad_index), note_name, (unsigned)midi_note);
  
  // Go back 2: pop note roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* note_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build options: C2, C#2, D2, ... C7 (MIDI 36-96 = 61 notes)
  static char options[512];
  options[0] = '\0';
  char* pos = options;
  size_t remaining = sizeof(options);
  
  for (uint8_t midi = 36; midi <= 96 && remaining > 8; midi++) {
    char note_name[8];
    get_note_display_name(midi, note_name, sizeof(note_name));
    int written = snprintf(pos, remaining, "%s%s", midi > 36 ? "\n" : "", note_name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  // Current selection: convert MIDI note to roller index
  uint8_t current_note = mapping->action.params.note.note;
  if (current_note < 36) current_note = 60;  // Default to C4
  if (current_note > 96) current_note = 96;
  uint32_t current_idx = current_note - 36;
  
  return menu_create_roller_page("Note", options, current_idx, 
    note_confirm_cb, NULL);
}

static void nav_to_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note", note_roller_create);
}

// ============================================================================
// Preset Hold (for ACTION_PRESET_HOLD)
// ============================================================================

static lv_obj_t* preset_hold_page_create(void);  // Forward declaration

// Helper to build preset options string for roller
static void build_preset_options(char* buf, size_t buf_size, uint16_t* count) {
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t preset_count = 128;  // Default
  if (device && device->pc_info) {
    preset_count = device->pc_info->count;
  }
  
  buf[0] = '\0';
  char* pos = buf;
  size_t remaining = buf_size;
  
  for (uint16_t i = 0; i < preset_count && remaining > 8; i++) {
    uint16_t display_num = i + 1;  // 1-based for user
    int written = snprintf(pos, remaining, "%s%u", i > 0 ? "\n" : "", (unsigned)display_num);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
  
  *count = preset_count;
}

static void preset_hold_press_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  mapping->action.params.preset_cycle.press_preset = index_base + selected_idx;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Preset Hold press set to %u", (unsigned)(index_base + selected_idx));
  
  // Go back to preset hold submenu
  menu_navigate_back_then_to(2, "Preset Hold", preset_hold_page_create);
}

static lv_obj_t* preset_hold_press_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  uint16_t count;
  build_preset_options(options, sizeof(options), &count);
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  uint16_t current = mapping->action.params.preset_cycle.press_preset;
  uint32_t current_idx = (current >= index_base) ? (current - index_base) : 0;
  if (current_idx >= count) current_idx = 0;
  
  return menu_create_roller_page("Press", options, current_idx,
    preset_hold_press_confirm_cb, NULL);
}

static void nav_to_preset_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", preset_hold_press_roller_create);
}

static void preset_hold_release_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  mapping->action.params.preset_cycle.release_preset = index_base + selected_idx;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Preset Hold release set to %u", (unsigned)(index_base + selected_idx));
  
  // Go back to preset hold submenu
  menu_navigate_back_then_to(2, "Preset Hold", preset_hold_page_create);
}

static lv_obj_t* preset_hold_release_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  uint16_t count;
  build_preset_options(options, sizeof(options), &count);
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  uint16_t current = mapping->action.params.preset_cycle.release_preset;
  uint32_t current_idx = (current >= index_base) ? (current - index_base) : 0;
  if (current_idx >= count) current_idx = 0;
  
  return menu_create_roller_page("Release", options, current_idx,
    preset_hold_release_confirm_cb, NULL);
}

static void nav_to_preset_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", preset_hold_release_roller_create);
}

static bool preset_hold_handle_back(void) {
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
  return true;
}

static lv_obj_t* preset_hold_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  int buf = get_next_buffer_set();
  
  // Get device for display
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  uint16_t press = mapping->action.params.preset_cycle.press_preset;
  uint16_t release = mapping->action.params.preset_cycle.release_preset;
  
  snprintf(s_preset_hold_press_label[buf], sizeof(s_preset_hold_press_label[buf]),
    "Press: %u", (unsigned)(press - index_base + 1));
  snprintf(s_preset_hold_release_label[buf], sizeof(s_preset_hold_release_label[buf]),
    "Release: %u", (unsigned)(release - index_base + 1));
  
  s_preset_hold_items[0] = (menu_item_t){
    s_preset_hold_press_label[buf], nav_to_preset_hold_press, NULL, true
  };
  s_preset_hold_items[1] = (menu_item_t){
    s_preset_hold_release_label[buf], nav_to_preset_hold_release, NULL, true
  };
  
  menu_set_custom_back_handler(preset_hold_handle_back);
  
  return menu_create_page("Preset Hold", s_preset_hold_items, 2);
}

// ============================================================================
// Preset Cycle (for ACTION_PRESET_CYCLE)
// ============================================================================

static void preset_cycle_steps_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t new_steps = selected_idx + 2;  // Range 2-8
  mapping->action.params.preset_cycle.num_presets = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Preset Cycle steps set to %u", (unsigned)new_steps);
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* preset_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Options: 2, 3, 4, 5, 6, 7, 8
  static const char* options = "2\n3\n4\n5\n6\n7\n8";
  
  uint8_t current_steps = mapping->action.params.preset_cycle.num_presets;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx,
    preset_cycle_steps_confirm_cb, NULL);
}

static void nav_to_preset_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", preset_cycle_steps_roller_create);
}

static void preset_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  uint8_t step = s_editing_preset_step;
  if (step < MAX_PRESET_CYCLE_STEPS) {
    mapping->action.params.preset_cycle.cycle_presets[step] = index_base + selected_idx;
    persist_scene_changes();
    
    ESP_LOGI(TAG, "Preset Cycle step %u set to preset %u", 
      (unsigned)step, (unsigned)(index_base + selected_idx));
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* preset_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  uint16_t count;
  build_preset_options(options, sizeof(options), &count);
  
  // Get device for index_base
  const device_def_t* device = (const device_def_t*)scene_get_device(
    scene_get_current_index());
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  uint8_t step = s_editing_preset_step;
  uint16_t current = mapping->action.params.preset_cycle.cycle_presets[step];
  uint32_t current_idx = (current >= index_base) ? (current - index_base) : 0;
  if (current_idx >= count) current_idx = 0;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, options, current_idx,
    preset_cycle_step_confirm_cb, NULL);
}

static void nav_to_preset_cycle_step(void* user_data) {
  s_editing_preset_step = (uint8_t)(uintptr_t)user_data;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_preset_step + 1));
  menu_navigate_to(title, preset_cycle_step_roller_create);
}

// ============================================================================
// Tempo Hold (for ACTION_TEMPO + VARIANT_HOLD)
// ============================================================================

static lv_obj_t* tempo_hold_page_create(void);  // Forward declaration

// Helper to build tempo options string for roller (20-300 BPM)
static void build_tempo_options(char* buf, size_t buf_size) {
  buf[0] = '\0';
  char* pos = buf;
  size_t remaining = buf_size;
  
  for (uint16_t bpm = 20; bpm <= 300 && remaining > 8; bpm++) {
    int written = snprintf(pos, remaining, "%s%u", bpm > 20 ? "\n" : "", (unsigned)bpm);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
}

static void tempo_hold_press_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint16_t bpm = 20 + selected_idx;  // Range 20-300
  mapping->action.params.tempo.press_bpm = bpm;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Tempo Hold press set to %u BPM", (unsigned)bpm);
  
  // Go back to tempo hold submenu
  menu_navigate_back_then_to(2, "Tempo Hold", tempo_hold_page_create);
}

static lv_obj_t* tempo_hold_press_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  build_tempo_options(options, sizeof(options));
  
  uint16_t current = mapping->action.params.tempo.press_bpm;
  if (current < 20) current = 120;
  if (current > 300) current = 120;
  uint32_t current_idx = current - 20;
  
  return menu_create_roller_page("Press", options, current_idx,
    tempo_hold_press_confirm_cb, NULL);
}

static void nav_to_tempo_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", tempo_hold_press_roller_create);
}

static void tempo_hold_release_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint16_t bpm = 20 + selected_idx;  // Range 20-300
  mapping->action.params.tempo.release_bpm = bpm;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Tempo Hold release set to %u BPM", (unsigned)bpm);
  
  // Go back to tempo hold submenu
  menu_navigate_back_then_to(2, "Tempo Hold", tempo_hold_page_create);
}

static lv_obj_t* tempo_hold_release_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  build_tempo_options(options, sizeof(options));
  
  uint16_t current = mapping->action.params.tempo.release_bpm;
  if (current < 20) current = 120;
  if (current > 300) current = 120;
  uint32_t current_idx = current - 20;
  
  return menu_create_roller_page("Release", options, current_idx,
    tempo_hold_release_confirm_cb, NULL);
}

static void nav_to_tempo_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", tempo_hold_release_roller_create);
}

static bool tempo_hold_handle_back(void) {
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
  return true;
}

static lv_obj_t* tempo_hold_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  int buf = get_next_buffer_set();
  
  uint16_t press = mapping->action.params.tempo.press_bpm;
  uint16_t release = mapping->action.params.tempo.release_bpm;
  if (press < 20 || press > 300) press = 120;
  if (release < 20 || release > 300) release = 120;
  
  snprintf(s_tempo_hold_press_label[buf], sizeof(s_tempo_hold_press_label[buf]),
    "Press: %u BPM", (unsigned)press);
  snprintf(s_tempo_hold_release_label[buf], sizeof(s_tempo_hold_release_label[buf]),
    "Release: %u BPM", (unsigned)release);
  
  s_tempo_hold_items[0] = (menu_item_t){
    s_tempo_hold_press_label[buf], nav_to_tempo_hold_press, NULL, true
  };
  s_tempo_hold_items[1] = (menu_item_t){
    s_tempo_hold_release_label[buf], nav_to_tempo_hold_release, NULL, true
  };
  
  menu_set_custom_back_handler(tempo_hold_handle_back);
  
  return menu_create_page("Tempo Hold", s_tempo_hold_items, 2);
}

// ============================================================================
// Tempo Cycle (for ACTION_TEMPO + VARIANT_CYCLE)
// ============================================================================

static void tempo_cycle_steps_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t new_steps = selected_idx + 2;  // Range 2-8
  mapping->action.params.tempo.num_tempos = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Tempo Cycle steps set to %u", (unsigned)new_steps);
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* tempo_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Options: 2, 3, 4, 5, 6, 7, 8
  static const char* options = "2\n3\n4\n5\n6\n7\n8";
  
  uint8_t current_steps = mapping->action.params.tempo.num_tempos;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx,
    tempo_cycle_steps_confirm_cb, NULL);
}

static void nav_to_tempo_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", tempo_cycle_steps_roller_create);
}

static void tempo_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t step = s_editing_tempo_step;
  if (step < MAX_TEMPO_CYCLE_STEPS) {
    uint16_t bpm = 20 + selected_idx;  // Range 20-300
    mapping->action.params.tempo.cycle_tempos[step] = bpm;
    persist_scene_changes();
    
    ESP_LOGI(TAG, "Tempo Cycle step %u set to %u BPM", (unsigned)step, (unsigned)bpm);
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* tempo_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[2048];
  build_tempo_options(options, sizeof(options));
  
  uint8_t step = s_editing_tempo_step;
  uint16_t current = mapping->action.params.tempo.cycle_tempos[step];
  if (current < 20 || current > 300) current = 120;
  uint32_t current_idx = current - 20;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, options, current_idx,
    tempo_cycle_step_confirm_cb, NULL);
}

static void nav_to_tempo_cycle_step(void* user_data) {
  s_editing_tempo_step = (uint8_t)(uintptr_t)user_data;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_tempo_step + 1));
  menu_navigate_to(title, tempo_cycle_step_roller_create);
}

// ============================================================================
// Touchwheel Mode Helpers (shared by Hold and Cycle)
// ============================================================================

// Helper to build touchwheel mode options string for roller
static void build_tw_mode_options(char* buf, size_t buf_size) {
  buf[0] = '\0';
  char* pos = buf;
  size_t remaining = buf_size;
  
  for (uint8_t i = 0; i < NUM_TOUCHWHEEL_USER_MODES && remaining > 20; i++) {
    const char* name = touchwheel_get_mode_name(i);
    int written = snprintf(pos, remaining, "%s%s", i > 0 ? "\n" : "", name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
  }
}

// ============================================================================
// TW Hold (for ACTION_TOUCHWHEEL_HOLD)
// ============================================================================

static lv_obj_t* tw_hold_page_create(void);  // Forward declaration

static void tw_hold_press_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    mapping->action.params.tw_mode.mode = (uint8_t)selected_idx;
    persist_scene_changes();
    ESP_LOGI(TAG, "TW Hold press set to %s", touchwheel_get_mode_name(selected_idx));
  }
  
  menu_navigate_back_then_to(2, "TW Hold", tw_hold_page_create);
}

static lv_obj_t* tw_hold_press_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = mapping->action.params.tw_mode.mode;
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  return menu_create_roller_page("Press", options, current, tw_hold_press_confirm_cb, NULL);
}

static void nav_to_tw_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", tw_hold_press_roller_create);
}

static void tw_hold_release_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    mapping->action.params.tw_mode.mode2 = (uint8_t)selected_idx;
    persist_scene_changes();
    ESP_LOGI(TAG, "TW Hold release set to %s", touchwheel_get_mode_name(selected_idx));
  }
  
  menu_navigate_back_then_to(2, "TW Hold", tw_hold_page_create);
}

static lv_obj_t* tw_hold_release_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = mapping->action.params.tw_mode.mode2;
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  return menu_create_roller_page("Release", options, current, tw_hold_release_confirm_cb, NULL);
}

static void nav_to_tw_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", tw_hold_release_roller_create);
}

static bool tw_hold_handle_back(void) {
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
  return true;
}

static lv_obj_t* tw_hold_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  int buf = get_next_buffer_set();
  
  uint8_t press_idx = mapping->action.params.tw_mode.mode;
  uint8_t release_idx = mapping->action.params.tw_mode.mode2;
  if (press_idx >= NUM_TOUCHWHEEL_USER_MODES) press_idx = 0;
  if (release_idx >= NUM_TOUCHWHEEL_USER_MODES) release_idx = 0;
  
  snprintf(s_tw_hold_press_label[buf], sizeof(s_tw_hold_press_label[buf]),
    "Press: %s", touchwheel_get_mode_name(press_idx));
  snprintf(s_tw_hold_release_label[buf], sizeof(s_tw_hold_release_label[buf]),
    "Release: %s", touchwheel_get_mode_name(release_idx));
  
  s_tw_hold_items[0] = (menu_item_t){
    s_tw_hold_press_label[buf], nav_to_tw_hold_press, NULL, true
  };
  s_tw_hold_items[1] = (menu_item_t){
    s_tw_hold_release_label[buf], nav_to_tw_hold_release, NULL, true
  };
  
  menu_set_custom_back_handler(tw_hold_handle_back);
  
  return menu_create_page("TW Hold", s_tw_hold_items, 2);
}

// ============================================================================
// TW Cycle (for ACTION_TOUCHWHEEL_CYCLE)
// ============================================================================

static void tw_cycle_steps_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t new_steps = selected_idx + 2;  // Range 2-8
  mapping->action.params.tw_mode.num_modes = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "TW Cycle steps set to %u", (unsigned)new_steps);
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* tw_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Options: 2, 3, 4, 5, 6, 7, 8
  static const char* options = "2\n3\n4\n5\n6\n7\n8";
  
  uint8_t current_steps = mapping->action.params.tw_mode.num_modes;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx,
    tw_cycle_steps_confirm_cb, NULL);
}

static void nav_to_tw_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", tw_cycle_steps_roller_create);
}

static void tw_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t step = s_editing_tw_step;
  if (step < MAX_TW_CYCLE_STEPS && selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    mapping->action.params.tw_mode.modes[step] = (uint8_t)selected_idx;
    persist_scene_changes();
    ESP_LOGI(TAG, "TW Cycle step %u set to %s", 
      (unsigned)step, touchwheel_get_mode_name(selected_idx));
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* tw_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t step = s_editing_tw_step;
  uint8_t current = mapping->action.params.tw_mode.modes[step];
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, options, current,
    tw_cycle_step_confirm_cb, NULL);
}

static void nav_to_tw_cycle_step(void* user_data) {
  s_editing_tw_step = (uint8_t)(uintptr_t)user_data;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_tw_step + 1));
  menu_navigate_to(title, tw_cycle_step_roller_create);
}

// ============================================================================
// Randomize Slot Roller (for ACTION_RANDOMIZE)
// ============================================================================

// Free filtered randomize CC options
static void free_randomize_cc_options(void) {
  if (s_randomize_cc_options.options_str) {
    heap_caps_free(s_randomize_cc_options.options_str);
    s_randomize_cc_options.options_str = NULL;
  }
  if (s_randomize_cc_options.cc_numbers) {
    heap_caps_free(s_randomize_cc_options.cc_numbers);
    s_randomize_cc_options.cc_numbers = NULL;
  }
  s_randomize_cc_options.count = 0;
}

// Check if a CC is already used in another randomize slot
static bool is_cc_used_in_randomize(const action_t* action, uint8_t cc_num, uint8_t exclude_slot) {
  uint8_t num_ccs = action->params.randomize.num_ccs;
  for (uint8_t i = 0; i < num_ccs && i < MAX_RANDOMIZE_SLOTS; i++) {
    if (i != exclude_slot && action->params.randomize.cc_numbers[i] == cc_num) {
      return true;
    }
  }
  return false;
}

// Build filtered CC options for randomize (excludes already-selected CCs)
static bool build_randomize_cc_options(const action_t* action, uint8_t editing_slot) {
  free_randomize_cc_options();
  
  // Ensure base CC options are loaded
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) return false;
  }
  
  // Allocate storage (same size as full options - we'll use less)
  s_randomize_cc_options.cc_numbers = heap_caps_calloc(
    s_cc_options.count, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  s_randomize_cc_options.options_str = heap_caps_calloc(
    s_cc_options.count * 28, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_randomize_cc_options.cc_numbers || !s_randomize_cc_options.options_str) {
    ESP_LOGE(TAG, "Failed to allocate filtered CC options");
    free_randomize_cc_options();
    return false;
  }
  
  // Always include "Inactive" as first option
  strcpy(s_randomize_cc_options.options_str, "Inactive");
  s_randomize_cc_options.cc_numbers[0] = 0xFF;
  s_randomize_cc_options.count = 1;
  
  size_t pos = strlen("Inactive");
  
  // Add CCs that aren't already used in other slots
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    uint8_t cc_num = s_cc_options.cc_numbers[i];
    
    // Skip if already used in another slot
    if (is_cc_used_in_randomize(action, cc_num, editing_slot)) {
      continue;
    }
    
    // Find the CC name from the original options string
    const char* src = s_cc_options.options_str;
    uint16_t line = 0;
    const char* line_start = src;
    
    // Find the i-th line in the original options
    while (*src && line < i) {
      if (*src == '\n') {
        line++;
        line_start = src + 1;
      }
      src++;
    }
    
    // Find end of this line
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    
    size_t name_len = line_end - line_start;
    if (name_len > 24) name_len = 24;
    
    // Append to filtered options
    s_randomize_cc_options.options_str[pos++] = '\n';
    memcpy(s_randomize_cc_options.options_str + pos, line_start, name_len);
    pos += name_len;
    s_randomize_cc_options.options_str[pos] = '\0';
    
    s_randomize_cc_options.cc_numbers[s_randomize_cc_options.count] = cc_num;
    s_randomize_cc_options.count++;
  }
  
  ESP_LOGD(TAG, "Built filtered randomize options: %u of %u CCs available",
    (unsigned)(s_randomize_cc_options.count - 1), (unsigned)(s_cc_options.count - 1));
  
  return true;
}

// ============================================================================
// Filtered CC options for Param Cycle (excludes already-selected CCs)
// ============================================================================

static void free_param_cc_options(void) {
  if (s_param_cc_options.options_str) {
    heap_caps_free(s_param_cc_options.options_str);
    s_param_cc_options.options_str = NULL;
  }
  if (s_param_cc_options.cc_numbers) {
    heap_caps_free(s_param_cc_options.cc_numbers);
    s_param_cc_options.cc_numbers = NULL;
  }
  s_param_cc_options.count = 0;
}

// Check if a CC is already used in another param slot (for cycle or hold)
static bool is_cc_used_in_param(const action_t* action, uint8_t cc_num, uint8_t exclude_slot) {
  if (action->type == ACTION_PARAM_CYCLE) {
    uint8_t num = action->params.tw_param.num_params;
    for (uint8_t i = 0; i < num && i < MAX_PARAM_CYCLE_STEPS; i++) {
      if (i != exclude_slot && action->params.tw_param.params[i] == cc_num)
        return true;
    }
  } else if (action->type == ACTION_PARAM_HOLD) {
    // For hold, slot 0 = param (press), slot 1 = param2 (release)
    if (exclude_slot != 0 && action->params.tw_param.param == cc_num) return true;
    if (exclude_slot != 1 && action->params.tw_param.param2 == cc_num) return true;
  }
  return false;
}

// Build filtered CC options for param actions (excludes already-selected CCs)
static bool build_param_cc_options(const action_t* action, uint8_t editing_slot) {
  free_param_cc_options();
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) return false;
  }
  
  s_param_cc_options.cc_numbers = heap_caps_calloc(
    s_cc_options.count, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  s_param_cc_options.options_str = heap_caps_calloc(
    s_cc_options.count * 28, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_param_cc_options.cc_numbers || !s_param_cc_options.options_str) {
    ESP_LOGE(TAG, "Failed to allocate filtered param CC options");
    free_param_cc_options();
    return false;
  }
  
  s_param_cc_options.count = 0;
  size_t pos = 0;
  
  // Add CCs that aren't already used in other slots
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    uint8_t cc_num = s_cc_options.cc_numbers[i];
    
    if (is_cc_used_in_param(action, cc_num, editing_slot))
      continue;
    
    // Find the CC name from the original options string
    const char* src = s_cc_options.options_str;
    uint16_t line = 0;
    const char* line_start = src;
    
    while (*src && line < i) {
      if (*src == '\n') {
        line++;
        line_start = src + 1;
      }
      src++;
    }
    
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    size_t name_len = line_end - line_start;
    
    // Append to filtered options
    if (s_param_cc_options.count > 0)
      s_param_cc_options.options_str[pos++] = '\n';
    memcpy(s_param_cc_options.options_str + pos, line_start, name_len);
    pos += name_len;
    s_param_cc_options.options_str[pos] = '\0';
    
    s_param_cc_options.cc_numbers[s_param_cc_options.count] = cc_num;
    s_param_cc_options.count++;
  }
  
  ESP_LOGD(TAG, "Built filtered param options: %u of %u CCs available",
    (unsigned)s_param_cc_options.count, (unsigned)(s_cc_options.count - 1));
  
  return true;
}

// Get next unused CC from device for auto-fill
static uint8_t get_next_unused_param_cc(const action_t* action, uint8_t exclude_slot) {
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) return 1;  // Fallback to CC 1
  }
  
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    uint8_t cc_num = s_cc_options.cc_numbers[i];
    if (!is_cc_used_in_param(action, cc_num, 0xFF))  // 0xFF = don't exclude any
      return cc_num;
  }
  
  return 1;  // Fallback
}

static void randomize_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t slot = s_editing_randomize_slot;
  if (slot >= MAX_RANDOMIZE_SLOTS) return;
  
  uint8_t num_ccs = mapping->action.params.randomize.num_ccs;
  
  if (selected_index == 0) {
    // "Inactive" selected - remove this slot by compacting
    if (slot < num_ccs) {
      // Shift all slots after this one down
      for (uint8_t i = slot; i < num_ccs - 1 && i < MAX_RANDOMIZE_SLOTS - 1; i++) {
        mapping->action.params.randomize.cc_numbers[i] = 
          mapping->action.params.randomize.cc_numbers[i + 1];
      }
      mapping->action.params.randomize.cc_numbers[num_ccs - 1] = 0;
      mapping->action.params.randomize.num_ccs = num_ccs - 1;
      ESP_LOGI(TAG, "Randomize slot %u cleared, %u slots remain", 
        (unsigned)slot, (unsigned)(num_ccs - 1));
    }
  } else {
    // CC selected from filtered list - use filtered mapping
    if (selected_index < s_randomize_cc_options.count) {
      uint8_t cc_num = s_randomize_cc_options.cc_numbers[selected_index];
      mapping->action.params.randomize.cc_numbers[slot] = cc_num;
      
      // If this is a new slot, increment num_ccs
      if (slot >= num_ccs && slot < MAX_RANDOMIZE_SLOTS) {
        mapping->action.params.randomize.num_ccs = slot + 1;
      }
      
      const device_def_t* device = (const device_def_t*)scene_get_device(
        scene_get_current_index());
      const char* cc_name = assets_get_cc_name(device, cc_num);
      ESP_LOGI(TAG, "Randomize slot %u set to CC%u (%s)", 
        (unsigned)slot, (unsigned)cc_num, cc_name ? cc_name : "?");
    }
  }
  
  // Free filtered options
  free_randomize_cc_options();
  
  persist_scene_changes();
  
  // Go back 2: pop roller, old pad detail, push fresh pad detail
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* randomize_cc_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = s_editing_randomize_slot;
  
  // Build filtered options (excludes CCs already used in other slots)
  if (!build_randomize_cc_options(&mapping->action, slot)) {
    return menu_create_page("Error", NULL, 0);
  }
  
  // Find current selection in filtered list
  uint32_t current_idx = 0;  // Default to "Inactive"
  uint8_t num_ccs = mapping->action.params.randomize.num_ccs;
  if (slot < num_ccs) {
    uint8_t cc_num = mapping->action.params.randomize.cc_numbers[slot];
    // Search in filtered list
    for (uint16_t i = 1; i < s_randomize_cc_options.count; i++) {
      if (s_randomize_cc_options.cc_numbers[i] == cc_num) {
        current_idx = i;
        break;
      }
    }
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  
  return menu_create_roller_page(title, s_randomize_cc_options.options_str, current_idx,
    randomize_cc_confirm_cb, NULL);
}

static void nav_to_randomize_slot(void* user_data) {
  uint8_t clicked_slot = (uint8_t)(uintptr_t)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t num_ccs = mapping->action.params.randomize.num_ccs;
  
  // If clicking on an inactive slot, redirect to the next available slot
  if (clicked_slot >= num_ccs) {
    s_editing_randomize_slot = num_ccs;
  } else {
    s_editing_randomize_slot = clicked_slot;
  }
  
  // Ensure CC options are loaded
  if (!s_cc_options.options_str) {
    load_cc_options();
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_randomize_slot + 1));
  menu_navigate_to(title, randomize_cc_roller_create);
}

// ============================================================================
// LFO Slot Roller (for LFO actions)
// ============================================================================

static const char* LFO_SLOT_OPTIONS = "LFO 1\nLFO 2\nBoth";

static void lfo_slot_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    menu_navigate_back();
    return;
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    menu_navigate_back();
    return;
  }
  
  // Map roller index to slot value: 0->1, 1->2, 2->3
  mapping->action.params.lfo.slot = (uint8_t)(selected_index + 1);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %u LFO slot set to %u", 
    (unsigned)s_editing_pad_index, (unsigned)mapping->action.params.lfo.slot);
  
  static char title[24];
  snprintf(title, sizeof(title), "%s", get_pad_display_name(s_editing_pad_index));
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* lfo_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t slot = mapping->action.params.lfo.slot;
  
  // Map slot value to roller index: 1->0, 2->1, 3->2
  uint32_t current_idx = (slot > 0 && slot <= 3) ? slot - 1 : 0;
  
  return menu_create_roller_page("Target", LFO_SLOT_OPTIONS, current_idx,
    lfo_slot_confirm_cb, NULL);
}

static void nav_to_lfo_slot(void* user_data) {
  (void)user_data;
  menu_navigate_to("Target", lfo_slot_roller_create);
}

// ============================================================================
// UI Module Roller (for ACTION_SET_UI, ACTION_UI_HOLD)
// ============================================================================

// Helper to get module display title from index
static const char* get_ui_module_display_name(uint8_t idx) {
  if (idx >= ui_scene_selectable_module_count) return "Unknown";
  return ui_get_module_title(ui_scene_selectable_modules[idx]);
}

// Build roller options string from selectable modules (using titles)
static void build_ui_module_options(char* buf, size_t buf_size) {
  buf[0] = '\0';
  for (int i = 0; i < ui_scene_selectable_module_count; i++) {
    if (i > 0) strncat(buf, "\n", buf_size - strlen(buf) - 1);
    const char* title = ui_get_module_title(ui_scene_selectable_modules[i]);
    strncat(buf, title, buf_size - strlen(buf) - 1);
  }
}

// For ACTION_SET_UI and ACTION_UI_HOLD press module
static void ui_module_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  mapping->action.params.ui.module = (uint8_t)selected_index;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %u UI module set to: %s",
    (unsigned)s_editing_pad_index,
    get_ui_module_display_name((uint8_t)selected_index));
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* ui_module_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint32_t current_idx = mapping->action.params.ui.module;
  if (current_idx >= (uint32_t)ui_scene_selectable_module_count)
    current_idx = 0;
  
  static char options[256];
  build_ui_module_options(options, sizeof(options));
  
  const char* roller_title = (mapping->action.type == ACTION_UI_HOLD)
    ? "On Press" : "Module";
  
  return menu_create_roller_page(roller_title, options, current_idx,
    ui_module_confirm_cb, NULL);
}

static void nav_to_ui_module(void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  const char* title = (mapping->action.type == ACTION_UI_HOLD)
    ? "On Press" : "Module";
  menu_navigate_to(title, ui_module_roller_create);
}

// For ACTION_UI_HOLD release module
static void ui_module2_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  mapping->action.params.ui.module2 = (uint8_t)selected_index;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %u UI release module set to: %s",
    (unsigned)s_editing_pad_index,
    get_ui_module_display_name((uint8_t)selected_index));
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* ui_module2_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint32_t current_idx = mapping->action.params.ui.module2;
  if (current_idx >= (uint32_t)ui_scene_selectable_module_count)
    current_idx = 0;
  
  static char options[256];
  build_ui_module_options(options, sizeof(options));
  
  return menu_create_roller_page("On Release", options, current_idx,
    ui_module2_confirm_cb, NULL);
}

static void nav_to_ui_module2(void* user_data) {
  (void)user_data;
  menu_navigate_to("On Release", ui_module2_roller_create);
}

// ============================================================================
// UI Cycle (for ACTION_UI_CYCLE)
// ============================================================================

static void ui_cycle_steps_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t new_steps = (uint8_t)(selected_idx + 2);  // Range 2-8
  mapping->action.params.ui.num_modules = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %u UI Cycle steps set to %u",
    (unsigned)s_editing_pad_index, (unsigned)new_steps);
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* ui_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Options: 2, 3, 4, 5, 6, 7, 8
  static const char* options = "2\n3\n4\n5\n6\n7\n8";
  
  uint8_t current_steps = mapping->action.params.ui.num_modules;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx,
    ui_cycle_steps_confirm_cb, NULL);
}

static void nav_to_ui_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", ui_cycle_steps_roller_create);
}

static void ui_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t step = s_editing_ui_step;
  if (step < MAX_UI_CYCLE_STEPS &&
      selected_idx < (uint32_t)ui_scene_selectable_module_count) {
    mapping->action.params.ui.modules[step] = (uint8_t)selected_idx;
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %u UI Cycle step %u set to %s",
      (unsigned)s_editing_pad_index, (unsigned)(step + 1),
      get_ui_module_display_name((uint8_t)selected_idx));
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* ui_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static char options[256];
  build_ui_module_options(options, sizeof(options));
  
  uint8_t step = s_editing_ui_step;
  uint8_t current = mapping->action.params.ui.modules[step];
  if (current >= (uint8_t)ui_scene_selectable_module_count) current = 0;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, options, current,
    ui_cycle_step_confirm_cb, NULL);
}

static void nav_to_ui_cycle_step(void* user_data) {
  s_editing_ui_step = (uint8_t)(uintptr_t)user_data;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_ui_step + 1));
  menu_navigate_to(title, ui_cycle_step_roller_create);
}

// ============================================================================
// Param Hold/Cycle Rollers (for ACTION_PARAM_HOLD, ACTION_PARAM_CYCLE)
// ============================================================================

// For ACTION_PARAM_HOLD - press CC (slot 0)
static void param_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  if (selected_index < s_param_cc_options.count) {
    uint8_t cc = s_param_cc_options.cc_numbers[selected_index];
    mapping->action.params.tw_param.param = cc;
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %u Param Hold press CC set to %u",
      (unsigned)s_editing_pad_index, (unsigned)cc);
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* param_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build filtered options excluding slot 1 (release CC)
  if (!build_param_cc_options(&mapping->action, 0)) {
    return menu_create_page("Error", NULL, 0);
  }
  
  // Find current selection in filtered list
  uint8_t cc = mapping->action.params.tw_param.param;
  uint32_t current_idx = 0;
  for (uint16_t i = 0; i < s_param_cc_options.count; i++) {
    if (s_param_cc_options.cc_numbers[i] == cc) {
      current_idx = i;
      break;
    }
  }
  
  return menu_create_roller_page("On Press", s_param_cc_options.options_str, current_idx,
    param_confirm_cb, NULL);
}

static void nav_to_param(void* user_data) {
  (void)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  menu_navigate_to("On Press", param_roller_create);
}

// For ACTION_PARAM_HOLD - release CC (slot 1)
static void param2_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  if (selected_index < s_param_cc_options.count) {
    uint8_t cc = s_param_cc_options.cc_numbers[selected_index];
    mapping->action.params.tw_param.param2 = cc;
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %u Param Hold release CC set to %u",
      (unsigned)s_editing_pad_index, (unsigned)cc);
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* param2_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build filtered options excluding slot 0 (press CC)
  if (!build_param_cc_options(&mapping->action, 1)) {
    return menu_create_page("Error", NULL, 0);
  }
  
  // Find current selection in filtered list
  uint8_t cc = mapping->action.params.tw_param.param2;
  uint32_t current_idx = 0;
  for (uint16_t i = 0; i < s_param_cc_options.count; i++) {
    if (s_param_cc_options.cc_numbers[i] == cc) {
      current_idx = i;
      break;
    }
  }
  
  return menu_create_roller_page("On Release", s_param_cc_options.options_str, current_idx,
    param2_confirm_cb, NULL);
}

static void nav_to_param2(void* user_data) {
  (void)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  menu_navigate_to("On Release", param2_roller_create);
}

// For ACTION_PARAM_CYCLE - steps selector
static void param_cycle_steps_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t old_steps = mapping->action.params.tw_param.num_params;
  uint8_t new_steps = (uint8_t)(selected_idx + 2);  // Range 2-8
  
  // Auto-fill any new slots with next available unused CCs
  if (new_steps > old_steps) {
    for (uint8_t i = old_steps; i < new_steps && i < MAX_PARAM_CYCLE_STEPS; i++) {
      uint8_t next_cc = get_next_unused_param_cc(&mapping->action, 0xFF);
      mapping->action.params.tw_param.params[i] = next_cc;
      // Temporarily bump count so next iteration sees this as used
      mapping->action.params.tw_param.num_params = i + 1;
      ESP_LOGI(TAG, "Auto-filled step %u with CC %u", (unsigned)(i + 1), (unsigned)next_cc);
    }
  }
  
  mapping->action.params.tw_param.num_params = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %u Param Cycle steps set to %u",
    (unsigned)s_editing_pad_index, (unsigned)new_steps);
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* param_cycle_steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  static const char* options = "2\n3\n4\n5\n6\n7\n8";
  
  uint8_t current_steps = mapping->action.params.tw_param.num_params;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx,
    param_cycle_steps_confirm_cb, NULL);
}

static void nav_to_param_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", param_cycle_steps_roller_create);
}

// For ACTION_PARAM_CYCLE - individual step CC
static void param_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t step = s_editing_param_step;
  if (step < MAX_PARAM_CYCLE_STEPS && selected_idx < s_param_cc_options.count) {
    uint8_t cc = s_param_cc_options.cc_numbers[selected_idx];
    mapping->action.params.tw_param.params[step] = cc;
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %u Param Cycle step %u set to CC %u",
      (unsigned)s_editing_pad_index, (unsigned)(step + 1), (unsigned)cc);
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* param_cycle_step_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  uint8_t step = s_editing_param_step;
  
  // Build filtered options (excludes CCs already used in other slots)
  if (!build_param_cc_options(&mapping->action, step)) {
    return menu_create_page("Error", NULL, 0);
  }
  
  // Find current selection in filtered list
  uint8_t cc = mapping->action.params.tw_param.params[step];
  uint32_t current_idx = 0;
  for (uint16_t i = 0; i < s_param_cc_options.count; i++) {
    if (s_param_cc_options.cc_numbers[i] == cc) {
      current_idx = i;
      break;
    }
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, s_param_cc_options.options_str, current_idx,
    param_cycle_step_confirm_cb, NULL);
}

static void nav_to_param_cycle_step(void* user_data) {
  s_editing_param_step = (uint8_t)(uintptr_t)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_param_step + 1));
  menu_navigate_to(title, param_cycle_step_roller_create);
}

// Helper to get CC name from number
static const char* get_cc_display_name(uint8_t cc_num) {
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  if (!device) return "Unknown";
  
  for (uint16_t i = 0; i < device->control_count; i++) {
    if (device->controls[i].type == MIDI_CONTROL_TYPE_CC &&
        device->controls[i].id == cc_num) {
      return device->controls[i].name ? device->controls[i].name : "CC";
    }
  }
  
  static char fallback[16];
  snprintf(fallback, sizeof(fallback), "CC %u", (unsigned)cc_num);
  return fallback;
}

// ============================================================================
// CC Slot Navigation (routes to correct handler based on action type)
// ============================================================================

static void nav_to_cc_slot(void* user_data) {
  uint8_t clicked_slot = (uint8_t)(uintptr_t)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t num_ccs = mapping->action.params.control.num_ccs;
  
  // If clicking on an inactive slot, redirect to the next available slot
  if (clicked_slot >= num_ccs) {
    s_editing_cc_slot = num_ccs;
  } else {
    s_editing_cc_slot = clicked_slot;
  }
  
  // Ensure CC options are loaded
  if (!s_cc_options.options_str) {
    load_cc_options();
  }
  
  static char title[24];
  
  // Route based on Control variant
  if (mapping->action.type == ACTION_CONTROL && mapping->action.variant == VARIANT_HOLD) {
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_hold_slot_page_create);
  } else if (mapping->action.type == ACTION_CONTROL && mapping->action.variant == VARIANT_CYCLE) {
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_cycle_slot_page_create);
  } else {
    snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_number_roller_create);
  }
}

// ============================================================================
// Pad Detail Page
// ============================================================================

static lv_obj_t* pad_detail_page_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    ESP_LOGW(TAG, "No current scene");
    return menu_create_page("Error", NULL, 0);
  }
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    ESP_LOGW(TAG, "No mapping for pad %u", (unsigned)s_editing_pad_index);
    return menu_create_page("Error", NULL, 0);
  }
  
  // Get next buffer set (ensures old pages keep their labels during async delete)
  int buf = get_next_buffer_set();
  
  int item_count = 0;
  
  // Action selector (always first) - 2-line format
  const char* action_name = get_action_display_name(mapping->action.type);
  snprintf(s_action_label[buf], sizeof(s_action_label[buf]), "Action\n%s", action_name);
  s_detail_items[item_count++] = (menu_item_t){s_action_label[buf], nav_to_action_type, NULL, true};
  
  // Show CC slots for the consolidated Control family
  if (mapping->action.type == ACTION_CONTROL) {

    // For CC Cycle, show Steps selector before the CC slots
    if (mapping->action.variant == VARIANT_CYCLE) {
      uint8_t steps = mapping->action.params.control.num_cycle_steps;
      if (steps < 2) steps = 2;
      snprintf(s_steps_label[buf], sizeof(s_steps_label[buf]), "Steps\n%u", (unsigned)steps);
      s_detail_items[item_count++] = (menu_item_t){
        s_steps_label[buf], nav_to_cc_cycle_steps, NULL, true
      };
    }

    for (int i = 0; i < 4; i++) {
      // Use appropriate display function based on variant
      const char* slot_display;
      if (mapping->action.variant == VARIANT_HOLD) {
        slot_display = get_cc_hold_slot_display(&mapping->action, (uint8_t)i);
      } else if (mapping->action.variant == VARIANT_CYCLE) {
        slot_display = get_cc_cycle_slot_display(&mapping->action, (uint8_t)i);
      } else {
        slot_display = get_cc_slot_display(&mapping->action, (uint8_t)i);
      }
      if (strcmp(slot_display, "Inactive") == 0) {
        // Inactive slot: show slot number (2-line format)
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]), 
          "Slot %d\nInactive", i + 1);
      } else {
        // Active slot: show slot number + control name (2-line format)
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]), 
          "Slot %d\n%s", i + 1, slot_display);
      }
      s_detail_items[item_count++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Preset selector for Program Set
  if (mapping->action.type == ACTION_PRESET) {
    uint16_t program = mapping->action.params.preset.program;
    
    // Get device for index_base
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    // Display as 1-based for user
    uint16_t display_num = program - index_base + 1;
    snprintf(s_preset_label[buf], sizeof(s_preset_label[buf]), "Preset\n%u", (unsigned)display_num);
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_label[buf], nav_to_program_set, NULL, true
    };
  }
  
  // Show Scene selector for Scene Set
  if (mapping->action.type == ACTION_SCENE) {
    uint8_t target = mapping->action.params.target.number;
    
    // Find scene name from manifest
    const char* target_name = NULL;
    uint16_t count = scene_get_total_count();
    for (uint16_t i = 0; i < count; i++) {
      if (scene_get_index_by_position(i) == target) {
        target_name = scene_get_name_by_position(i);
        break;
      }
    }
    
    if (target_name) {
      snprintf(s_scene_label[buf], sizeof(s_scene_label[buf]), "Scene\n%.28s", target_name);
    } else {
      snprintf(s_scene_label[buf], sizeof(s_scene_label[buf]), "Scene\n%u", (unsigned)target);
    }
    s_detail_items[item_count++] = (menu_item_t){
      s_scene_label[buf], nav_to_scene_set, NULL, true
    };
  }
  
  // Show Preset Hold submenu link
  if (mapping->action.type == ACTION_PRESET_HOLD) {
    // Get device for index_base
    const device_def_t* device = (const device_def_t*)scene_get_device(
      scene_get_current_index());
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    uint16_t press = mapping->action.params.preset_cycle.press_preset;
    uint16_t release = mapping->action.params.preset_cycle.release_preset;
    
    snprintf(s_preset_hold_press_label[buf], sizeof(s_preset_hold_press_label[buf]),
      "Press\n%u", (unsigned)(press - index_base + 1));
    snprintf(s_preset_hold_release_label[buf], sizeof(s_preset_hold_release_label[buf]),
      "Release\n%u", (unsigned)(release - index_base + 1));
    
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_hold_press_label[buf], nav_to_preset_hold_press, NULL, true
    };
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_hold_release_label[buf], nav_to_preset_hold_release, NULL, true
    };
  }
  
  // Show Preset Cycle submenu items
  if (mapping->action.type == ACTION_PRESET_CYCLE) {
    // Get device for index_base
    const device_def_t* device = (const device_def_t*)scene_get_device(
      scene_get_current_index());
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    uint8_t num_steps = mapping->action.params.preset_cycle.num_presets;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    // Steps selector
    snprintf(s_preset_cycle_steps_label[buf], sizeof(s_preset_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_cycle_steps_label[buf], nav_to_preset_cycle_steps, NULL, true
    };
    
    // Individual step items
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint16_t preset = mapping->action.params.preset_cycle.cycle_presets[i];
      snprintf(s_preset_cycle_step_labels[buf][i], sizeof(s_preset_cycle_step_labels[buf][i]),
        "Step %d\n%u", i + 1, (unsigned)(preset - index_base + 1));
      s_detail_items[item_count++] = (menu_item_t){
        s_preset_cycle_step_labels[buf][i], nav_to_preset_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // ACTION_TEMPO (consolidated): variant-conditional rows. The variant
  // picker itself lives in the action_config flow now; this legacy
  // detail-page path only renders the value rows for whichever variant is
  // already set, leaving variant changes to the new UI.
  if (mapping->action.type == ACTION_TEMPO && mapping->action.variant == VARIANT_HOLD) {
    uint16_t press = mapping->action.params.tempo.press_bpm;
    uint16_t release = mapping->action.params.tempo.release_bpm;
    if (press < 20 || press > 300) press = 120;
    if (release < 20 || release > 300) release = 120;

    snprintf(s_tempo_hold_press_label[buf], sizeof(s_tempo_hold_press_label[buf]),
      "Press\n%u BPM", (unsigned)press);
    snprintf(s_tempo_hold_release_label[buf], sizeof(s_tempo_hold_release_label[buf]),
      "Release\n%u BPM", (unsigned)release);

    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_hold_press_label[buf], nav_to_tempo_hold_press, NULL, true
    };
    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_hold_release_label[buf], nav_to_tempo_hold_release, NULL, true
    };
  }

  if (mapping->action.type == ACTION_TEMPO && mapping->action.variant == VARIANT_CYCLE) {
    uint8_t num_steps = mapping->action.params.tempo.num_tempos;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;

    snprintf(s_tempo_cycle_steps_label[buf], sizeof(s_tempo_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_cycle_steps_label[buf], nav_to_tempo_cycle_steps, NULL, true
    };

    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint16_t bpm = mapping->action.params.tempo.cycle_tempos[i];
      if (bpm < 20 || bpm > 300) bpm = 120;
      snprintf(s_tempo_cycle_step_labels[buf][i], sizeof(s_tempo_cycle_step_labels[buf][i]),
        "Step %d\n%u BPM", i + 1, (unsigned)bpm);
      s_detail_items[item_count++] = (menu_item_t){
        s_tempo_cycle_step_labels[buf][i], nav_to_tempo_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show TW Hold submenu items
  if (mapping->action.type == ACTION_TOUCHWHEEL_HOLD) {
    uint8_t press_idx = mapping->action.params.tw_mode.mode;
    uint8_t release_idx = mapping->action.params.tw_mode.mode2;
    if (press_idx >= NUM_TOUCHWHEEL_USER_MODES) press_idx = 0;
    if (release_idx >= NUM_TOUCHWHEEL_USER_MODES) release_idx = 0;
    
    snprintf(s_tw_hold_press_label[buf], sizeof(s_tw_hold_press_label[buf]),
      "Press\n%s", touchwheel_get_mode_name(press_idx));
    snprintf(s_tw_hold_release_label[buf], sizeof(s_tw_hold_release_label[buf]),
      "Release\n%s", touchwheel_get_mode_name(release_idx));
    
    s_detail_items[item_count++] = (menu_item_t){
      s_tw_hold_press_label[buf], nav_to_tw_hold_press, NULL, true
    };
    s_detail_items[item_count++] = (menu_item_t){
      s_tw_hold_release_label[buf], nav_to_tw_hold_release, NULL, true
    };
  }
  
  // Show TW Cycle submenu items
  if (mapping->action.type == ACTION_TOUCHWHEEL_CYCLE) {
    uint8_t num_steps = mapping->action.params.tw_mode.num_modes;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    // Steps selector
    snprintf(s_tw_cycle_steps_label[buf], sizeof(s_tw_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_tw_cycle_steps_label[buf], nav_to_tw_cycle_steps, NULL, true
    };
    
    // Individual step items
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint8_t mode_idx = mapping->action.params.tw_mode.modes[i];
      if (mode_idx >= NUM_TOUCHWHEEL_USER_MODES) mode_idx = 0;
      snprintf(s_tw_cycle_step_labels[buf][i], sizeof(s_tw_cycle_step_labels[buf][i]),
        "Step %d\n%s", i + 1, touchwheel_get_mode_name(mode_idx));
      s_detail_items[item_count++] = (menu_item_t){
        s_tw_cycle_step_labels[buf][i], nav_to_tw_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Tempo selector for ACTION_TEMPO + VARIANT_SET (consolidated)
  if (mapping->action.type == ACTION_TEMPO && mapping->action.variant == VARIANT_SET) {
    uint16_t bpm = mapping->action.params.tempo.bpm;
    if (bpm < 20 || bpm > 300) bpm = 120;
    snprintf(s_tempo_label[buf], sizeof(s_tempo_label[buf]), "Tempo\n%u BPM", (unsigned)bpm);
    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_label[buf], nav_to_set_tempo, NULL, true
    };
  }
  
  // Show Note selector for Note action
  if (mapping->action.type == ACTION_NOTE) {
    uint8_t midi_note = mapping->action.params.note.note;
    if (midi_note < 36 || midi_note > 96) midi_note = 60;  // Default to C4
    char note_name[8];
    get_note_display_name(midi_note, note_name, sizeof(note_name));
    snprintf(s_note_label[buf], sizeof(s_note_label[buf]), "Note\n%s", note_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_note_label[buf], nav_to_note, NULL, true
    };
  }
  
  // Show CC slots for Randomize action (limited by available device CCs)
  if (mapping->action.type == ACTION_RANDOMIZE) {
    uint8_t num_ccs = mapping->action.params.randomize.num_ccs;
    const device_def_t* device = (const device_def_t*)scene_get_device(
      scene_get_current_index());
    
    // Count available CC controls from device
    uint8_t available_ccs = 0;
    if (device) {
      for (uint16_t c = 0; c < device->control_count; c++) {
        if (device->controls[c].type == MIDI_CONTROL_TYPE_CC) {
          available_ccs++;
        }
      }
    }
    
    // Limit slots to min(MAX_RANDOMIZE_SLOTS, available_ccs)
    uint8_t max_slots = available_ccs < MAX_RANDOMIZE_SLOTS ? available_ccs : MAX_RANDOMIZE_SLOTS;
    if (max_slots == 0) max_slots = MAX_RANDOMIZE_SLOTS;  // Fallback if no device
    
    for (int i = 0; i < max_slots && item_count < MAX_DETAIL_ITEMS; i++) {
      if (i < num_ccs) {
        // Active slot: show slot + CC name (2-line format)
        uint8_t cc_num = mapping->action.params.randomize.cc_numbers[i];
        const char* cc_name = assets_get_cc_name(device, cc_num);
        if (cc_name && strcmp(cc_name, "Undefined") != 0) {
          snprintf(s_randomize_slot_labels[buf][i], 
            sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\n%s", i + 1, cc_name);
        } else {
          snprintf(s_randomize_slot_labels[buf][i], 
            sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\nCC%u", i + 1, (unsigned)cc_num);
        }
      } else {
        // Inactive slot
        snprintf(s_randomize_slot_labels[buf][i], 
          sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\nInactive", i + 1);
      }
      s_detail_items[item_count++] = (menu_item_t){
        s_randomize_slot_labels[buf][i], nav_to_randomize_slot, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show LFO slot selector for LFO actions
  if (mapping->action.type == ACTION_LFO_START || mapping->action.type == ACTION_LFO_STOP ||
      mapping->action.type == ACTION_LFO_TOGGLE || mapping->action.type == ACTION_LFO_SHAPE) {
    uint8_t slot = mapping->action.params.lfo.slot;
    const char* slot_name;
    switch (slot) {
      case 1: slot_name = "LFO 1"; break;
      case 2: slot_name = "LFO 2"; break;
      case 3: slot_name = "Both"; break;
      default: slot_name = "LFO 1"; mapping->action.params.lfo.slot = 1; break;  // Fix invalid default
    }
    snprintf(s_lfo_slot_label[buf], sizeof(s_lfo_slot_label[buf]), "Target\n%s", slot_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_lfo_slot_label[buf], nav_to_lfo_slot, NULL, true
    };
  }
  
  // Show UI module selector for SET_UI action
  if (mapping->action.type == ACTION_SET_UI && item_count < MAX_DETAIL_ITEMS) {
    const char* mod_name = get_ui_module_display_name(mapping->action.params.ui.module);
    snprintf(s_ui_module_label[buf], sizeof(s_ui_module_label[buf]),
      "Module\n%s", mod_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_ui_module_label[buf], nav_to_ui_module, NULL, true
    };
  }
  
  // Show UI module selectors for UI_HOLD action (press and release modules)
  if (mapping->action.type == ACTION_UI_HOLD && item_count < MAX_DETAIL_ITEMS - 1) {
    const char* press_name = get_ui_module_display_name(mapping->action.params.ui.module);
    snprintf(s_ui_module_label[buf], sizeof(s_ui_module_label[buf]),
      "On Press\n%s", press_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_ui_module_label[buf], nav_to_ui_module, NULL, true
    };
    
    const char* release_name = get_ui_module_display_name(mapping->action.params.ui.module2);
    snprintf(s_ui_module2_label[buf], sizeof(s_ui_module2_label[buf]),
      "On Release\n%s", release_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_ui_module2_label[buf], nav_to_ui_module2, NULL, true
    };
  }
  
  // Show UI Cycle submenu items
  if (mapping->action.type == ACTION_UI_CYCLE) {
    uint8_t num_steps = mapping->action.params.ui.num_modules;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    // Steps selector
    if (item_count < MAX_DETAIL_ITEMS) {
      snprintf(s_ui_cycle_steps_label[buf], sizeof(s_ui_cycle_steps_label[buf]),
        "Steps\n%u", (unsigned)num_steps);
      s_detail_items[item_count++] = (menu_item_t){
        s_ui_cycle_steps_label[buf], nav_to_ui_cycle_steps, NULL, true
      };
    }
    
    // Individual step items
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint8_t mod_idx = mapping->action.params.ui.modules[i];
      if (mod_idx >= (uint8_t)ui_scene_selectable_module_count) mod_idx = 0;
      snprintf(s_ui_cycle_step_labels[buf][i], sizeof(s_ui_cycle_step_labels[buf][i]),
        "Step %d\n%s", i + 1, get_ui_module_display_name(mod_idx));
      s_detail_items[item_count++] = (menu_item_t){
        s_ui_cycle_step_labels[buf][i], nav_to_ui_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Param Hold CC selectors (press and release)
  if (mapping->action.type == ACTION_PARAM_HOLD && item_count < MAX_DETAIL_ITEMS - 1) {
    const char* press_name = get_cc_display_name(mapping->action.params.tw_param.param);
    snprintf(s_param_label[buf], sizeof(s_param_label[buf]),
      "On Press\n%s", press_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_param_label[buf], nav_to_param, NULL, true
    };
    
    const char* release_name = get_cc_display_name(mapping->action.params.tw_param.param2);
    snprintf(s_param2_label[buf], sizeof(s_param2_label[buf]),
      "On Release\n%s", release_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_param2_label[buf], nav_to_param2, NULL, true
    };
  }
  
  // Show Param Cycle submenu items
  if (mapping->action.type == ACTION_PARAM_CYCLE) {
    uint8_t num_steps = mapping->action.params.tw_param.num_params;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    // Steps selector
    if (item_count < MAX_DETAIL_ITEMS) {
      snprintf(s_param_cycle_steps_label[buf], sizeof(s_param_cycle_steps_label[buf]),
        "Steps\n%u", (unsigned)num_steps);
      s_detail_items[item_count++] = (menu_item_t){
        s_param_cycle_steps_label[buf], nav_to_param_cycle_steps, NULL, true
      };
    }
    
    // Individual step items
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint8_t cc = mapping->action.params.tw_param.params[i];
      snprintf(s_param_cycle_step_labels[buf][i], sizeof(s_param_cycle_step_labels[buf][i]),
        "Step %d\n%s", i + 1, get_cc_display_name(cc));
      s_detail_items[item_count++] = (menu_item_t){
        s_param_cycle_step_labels[buf][i], nav_to_param_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  
  // Set custom back handler to recreate Pads list when going back
  menu_set_custom_back_handler(pad_detail_handle_back);
  
  return menu_create_page_2line(title, s_detail_items, item_count);
}

// ============================================================================
// Main Pads Page
// ============================================================================

lv_obj_t* menu_page_pads_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    ESP_LOGW(TAG, "No current scene");
    return menu_create_page("Pads", NULL, 0);
  }
  
  // Pre-load CC options from device
  load_cc_options();
  
  // Get next buffer set for labels
  int buf = get_next_buffer_set();
  
  int item_count = 0;
  
  // Determine which pads to show based on touchwheel mode
  bool show_touchwheel_pads = (scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS);
  
  if (show_touchwheel_pads) {
    // Show all 12 pads
    for (int i = 0; i < 12; i++) {
      touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
        scene_get_current_index(), (uint8_t)i);
      action_t* action = mapping ? &mapping->action : NULL;
      
      format_pad_label((uint8_t)i, action, s_pad_labels[buf][item_count], 
        sizeof(s_pad_labels[buf][item_count]));
      s_pad_items[item_count] = (menu_item_t){
        s_pad_labels[buf][item_count], nav_to_pad_detail, (void*)(uintptr_t)i, true
      };
      item_count++;
    }
  } else {
    // Show only 4 pads: Omega (8), Alpha (9), Beta (10), Gamma (11)
    for (int i = 8; i < 12; i++) {
      touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
        scene_get_current_index(), (uint8_t)i);
      action_t* action = mapping ? &mapping->action : NULL;
      
      format_pad_label((uint8_t)i, action, s_pad_labels[buf][item_count], 
        sizeof(s_pad_labels[buf][item_count]));
      s_pad_items[item_count] = (menu_item_t){
        s_pad_labels[buf][item_count], nav_to_pad_detail, (void*)(uintptr_t)i, true
      };
      item_count++;
    }
  }
  
  ESP_LOGI(TAG, "Pads page: showing %d pads (mode=%s)", item_count,
    show_touchwheel_pads ? "pads" : "other");
  
  return menu_create_page_2line("Pads", s_pad_items, item_count);
}

// Cleanup function (call when leaving pads menu)
void menu_page_pads_cleanup(void) {
  free_cc_options();
  s_pending_control = NULL;
  action_config_cleanup();
}
