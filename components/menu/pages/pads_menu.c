#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "action.h"
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
#define MAX_DETAIL_ITEMS 10  // action + steps + 4 slots + extra
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

// Re-entry guard
static bool s_callback_in_progress = false;

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
    "1", "2", "3", "4", "5", "6", "7", "8",  // Touchwheel segments
    "Omega",   // Pad 8 - main activation
    "Alpha",   // Pad 9
    "Beta",    // Pad 10
    "Gamma"    // Pad 11
  };
  if (index < 12) return names[index];
  return "?";
}

// ============================================================================
// Action Type Names for Roller
// ============================================================================

// Action types in display order
static const action_type_t s_action_types[] = {
  ACTION_NONE,
  ACTION_SEND_CC,
  ACTION_SEND_CC_HOLD,
  ACTION_SEND_CC_CYCLE,
  ACTION_PROGRAM_NEXT,
  ACTION_PROGRAM_PREV,
  ACTION_PROGRAM_SET,
  ACTION_SCENE_NEXT,
  ACTION_SCENE_PREV,
  ACTION_SCENE_SET,
  ACTION_PLAY,
  ACTION_STOP,
  ACTION_PAUSE,
  ACTION_RECORD,
  ACTION_TAP,
  ACTION_TAP_TEMPO,
  ACTION_SET_TEMPO,
  ACTION_TEMPO_INC,
  ACTION_TEMPO_DEC,
  ACTION_SEND_NOTE_ON,
  ACTION_SEND_NOTE_OFF,
  ACTION_RANDOMIZE_CC,
  ACTION_CONFIRM_PENDING,
  ACTION_RESET,
  ACTION_SUSTAIN,
  ACTION_SOSTENUTO,
  ACTION_TOUCHWHEEL_MODE,
  ACTION_TOUCHWHEEL_MODE_HOLD,
  ACTION_TOUCHWHEEL_MODE_CYCLE,
};
#define NUM_ACTION_TYPES (sizeof(s_action_types) / sizeof(s_action_types[0]))

static const char* get_action_display_name(action_type_t type) {
  switch (type) {
    case ACTION_NONE: return "<None>";
    case ACTION_SEND_CC: return "Send CC";
    case ACTION_SEND_CC_HOLD: return "CC Hold";
    case ACTION_SEND_CC_CYCLE: return "CC Cycle";
    case ACTION_PROGRAM_NEXT: return "Program Next";
    case ACTION_PROGRAM_PREV: return "Program Prev";
    case ACTION_PROGRAM_SET: return "PC";
    case ACTION_SCENE_NEXT: return "Scene Next";
    case ACTION_SCENE_PREV: return "Scene Prev";
    case ACTION_SCENE_SET: return "Scene Set";
    case ACTION_PLAY: return "Play";
    case ACTION_STOP: return "Stop";
    case ACTION_PAUSE: return "Pause";
    case ACTION_RECORD: return "Record";
    case ACTION_TAP: return "Tap";
    case ACTION_TAP_TEMPO: return "Tap Tempo";
    case ACTION_SET_TEMPO: return "Set Tempo";
    case ACTION_TEMPO_INC: return "Tempo +1";
    case ACTION_TEMPO_DEC: return "Tempo -1";
    case ACTION_SEND_NOTE_ON: return "Note On";
    case ACTION_SEND_NOTE_OFF: return "Note Off";
    case ACTION_RANDOMIZE_CC: return "Randomize CC";
    case ACTION_CONFIRM_PENDING: return "Confirm Pending";
    case ACTION_RESET: return "Reset";
    case ACTION_SUSTAIN: return "Sustain";
    case ACTION_SOSTENUTO: return "Sostenuto";
    case ACTION_TOUCHWHEEL_MODE: return "TW Mode";
    case ACTION_TOUCHWHEEL_MODE_HOLD: return "TW Mode Hold";
    case ACTION_TOUCHWHEEL_MODE_CYCLE: return "TW Mode Cycle";
    default: return "Unknown";
  }
}

static uint32_t action_type_to_roller_index(action_type_t type) {
  for (size_t i = 0; i < NUM_ACTION_TYPES; i++) {
    if (s_action_types[i] == type) return (uint32_t)i;
  }
  return 0;
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
    snprintf(buf, buf_size, "%s [%s]", name, action_name);
  } else {
    snprintf(buf, buf_size, "%s []", name);
  }
}

// Get display name for a CC slot: "ControlName: ValueName" or "ControlName: 64"
static const char* get_cc_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  
  // Slot is inactive if it's beyond num_ccs (consecutive slots from 0)
  if (slot >= action->params.cc.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.cc.cc_numbers[slot];
  uint8_t cc_val = action->params.cc.values[slot];
  
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
  if (slot >= action->params.cc.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.cc.cc_numbers[slot];
  
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
  if (slot >= action->params.cc.num_ccs) {
    return "Inactive";
  }
  
  uint8_t cc_num = action->params.cc.cc_numbers[slot];
  
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
  
  // Use a fixed title from the names array (string literals, safe)
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_to(title, pad_detail_page_create);
}

// ============================================================================
// Action Type Roller
// ============================================================================

static void action_type_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene || selected_index >= NUM_ACTION_TYPES) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_type_t new_type = s_action_types[selected_index];
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // If changing action type, reset the action
  if (mapping->action.type != new_type) {
    memset(&mapping->action, 0, sizeof(action_t));
    mapping->action.type = new_type;
    mapping->enabled = (new_type != ACTION_NONE);
    
    // CC slots are already 0 (inactive) from memset
    // Set default cycle steps for CC Cycle
    if (new_type == ACTION_SEND_CC_CYCLE) {
      mapping->action.params.cc.num_cycle_steps = 2;
    }
    
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %s action set to: %s", 
      get_pad_display_name(s_editing_pad_index), get_action_display_name(new_type));
  }
  
  s_callback_in_progress = false;
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  menu_navigate_back_then_to(2, title, pad_detail_page_create);
}

static lv_obj_t* action_type_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return NULL;
  
  // Build options string
  static char options[512];
  options[0] = '\0';
  for (size_t i = 0; i < NUM_ACTION_TYPES; i++) {
    if (i > 0) strcat(options, "\n");
    strcat(options, get_action_display_name(s_action_types[i]));
  }
  
  uint32_t current_idx = action_type_to_roller_index(mapping->action.type);
  
  return menu_create_roller_page("Action", options, current_idx, action_type_confirm_cb, NULL);
}

static void nav_to_action_type(void* user_data) {
  (void)user_data;
  menu_navigate_to("Action", action_type_roller_create);
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
  mapping->action.params.cc.cc_numbers[slot] = s_pending_cc_number;
  mapping->action.params.cc.values[slot] = cc_value;
  
  // If this slot was inactive (slot >= num_ccs), we're adding a new CC
  // New CCs always go at position num_ccs, so just increment
  if (slot >= mapping->action.params.cc.num_ccs) {
    mapping->action.params.cc.num_ccs = slot + 1;
  }
  
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC slot %u set to CC%u=%u",
    get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1),
    (unsigned)s_pending_cc_number, (unsigned)cc_value);
  
  s_pending_control = NULL;
  s_callback_in_progress = false;
  
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
  uint8_t current_val = mapping->action.params.cc.values[slot];
  
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
        uint8_t num_ccs = mapping->action.params.cc.num_ccs;
        
        // Only clear if this slot is actually active
        if (slot < num_ccs) {
          // Compact: shift slots after this one down
          for (int i = slot; i < 3; i++) {
            mapping->action.params.cc.cc_numbers[i] = mapping->action.params.cc.cc_numbers[i + 1];
            mapping->action.params.cc.values[i] = mapping->action.params.cc.values[i + 1];
            mapping->action.params.cc.values2[i] = mapping->action.params.cc.values2[i + 1];
          }
          // Clear the last slot
          mapping->action.params.cc.cc_numbers[3] = 0;
          mapping->action.params.cc.values[3] = 0;
          mapping->action.params.cc.values2[3] = 0;
          
          // Decrement num_ccs
          if (num_ccs > 0) {
            mapping->action.params.cc.num_ccs = num_ccs - 1;
          }
          
          persist_scene_changes();
          ESP_LOGI(TAG, "Pad %s CC slot %u cleared (compacted to %u)",
            get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1),
            (unsigned)mapping->action.params.cc.num_ccs);
        }
      }
    }
    
    s_callback_in_progress = false;
    
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
      if (slot < mapping->action.params.cc.num_ccs) {
        uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
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
  if (slot < mapping->action.params.cc.num_ccs) {
    uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
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
    uint8_t num_ccs = mapping->action.params.cc.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        mapping->action.params.cc.cc_numbers[i] = mapping->action.params.cc.cc_numbers[i + 1];
        mapping->action.params.cc.values[i] = mapping->action.params.cc.values[i + 1];
        mapping->action.params.cc.values2[i] = mapping->action.params.cc.values2[i + 1];
      }
      mapping->action.params.cc.cc_numbers[3] = 0;
      mapping->action.params.cc.values[3] = 0;
      mapping->action.params.cc.values2[3] = 0;
      if (num_ccs > 0) {
        mapping->action.params.cc.num_ccs = num_ccs - 1;
      }
      persist_scene_changes();
      ESP_LOGI(TAG, "Pad %s CC Hold slot %u cleared",
        get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    const char* title = get_pad_display_name(s_editing_pad_index);
    menu_navigate_back_then_to(3, title, pad_detail_page_create);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    mapping->action.params.cc.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= mapping->action.params.cc.num_ccs) {
      mapping->action.params.cc.values[slot] = 127;   // Default press value
      mapping->action.params.cc.values2[slot] = 0;    // Default release value
      mapping->action.params.cc.num_ccs = slot + 1;
    }
    
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %s CC Hold slot %u CC set to %u",
      get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Go back to pad detail page (refreshes slot display)
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
  
  if (slot < mapping->action.params.cc.num_ccs) {
    uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  
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
  
  mapping->action.params.cc.values[slot] = press_val;
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.cc.values[slot];
  
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  
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
  
  mapping->action.params.cc.values2[slot] = release_val;
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.cc.values2[slot];
  
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
  bool is_active = (slot < mapping->action.params.cc.num_ccs);
  uint8_t cc_num = is_active ? mapping->action.params.cc.cc_numbers[slot] : 0;
  uint8_t press_val = is_active ? mapping->action.params.cc.values[slot] : 0;
  uint8_t release_val = is_active ? mapping->action.params.cc.values2[slot] : 0;
  
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
    uint8_t num_ccs = mapping->action.params.cc.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        mapping->action.params.cc.cc_numbers[i] = mapping->action.params.cc.cc_numbers[i + 1];
        for (int s = 0; s < MAX_CYCLE_STEPS; s++) {
          mapping->action.params.cc.cycle_values[i][s] = 
            mapping->action.params.cc.cycle_values[i + 1][s];
        }
      }
      mapping->action.params.cc.cc_numbers[3] = 0;
      memset(mapping->action.params.cc.cycle_values[3], 0, MAX_CYCLE_STEPS);
      if (num_ccs > 0) {
        mapping->action.params.cc.num_ccs = num_ccs - 1;
      }
      persist_scene_changes();
      ESP_LOGI(TAG, "Pad %s CC Cycle slot %u cleared",
        get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    const char* title = get_pad_display_name(s_editing_pad_index);
    menu_navigate_back_then_to(3, title, pad_detail_page_create);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    mapping->action.params.cc.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= mapping->action.params.cc.num_ccs) {
      // Default cycle values: 0, 127 for 2 steps
      uint8_t steps = mapping->action.params.cc.num_cycle_steps;
      if (steps < 2) {
        mapping->action.params.cc.num_cycle_steps = 2;
        steps = 2;
      }
      mapping->action.params.cc.cycle_values[slot][0] = 0;
      mapping->action.params.cc.cycle_values[slot][1] = 127;
      mapping->action.params.cc.num_ccs = slot + 1;
    }
    
    persist_scene_changes();
    ESP_LOGI(TAG, "Pad %s CC Cycle slot %u CC set to %u",
      get_pad_display_name(s_editing_pad_index), (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Go back to pad detail page (refreshes slot display)
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
  
  if (slot < mapping->action.params.cc.num_ccs) {
    uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  
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
  
  mapping->action.params.cc.cycle_values[slot][step] = step_val;
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
  uint8_t cc_num = mapping->action.params.cc.cc_numbers[slot];
  uint8_t current_val = mapping->action.params.cc.cycle_values[slot][step];
  
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
  bool is_active = (slot < mapping->action.params.cc.num_ccs);
  uint8_t cc_num = is_active ? mapping->action.params.cc.cc_numbers[slot] : 0;
  uint8_t num_steps = mapping->action.params.cc.num_cycle_steps;
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
      uint8_t step_val = mapping->action.params.cc.cycle_values[slot][i];
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
  mapping->action.params.cc.num_cycle_steps = new_steps;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Pad %s CC Cycle steps set to %u",
    get_pad_display_name(s_editing_pad_index), (unsigned)new_steps);
  
  s_callback_in_progress = false;
  
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
  
  uint8_t current_steps = mapping->action.params.cc.num_cycle_steps;
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
// CC Slot Navigation (routes to correct handler based on action type)
// ============================================================================

static void nav_to_cc_slot(void* user_data) {
  uint8_t clicked_slot = (uint8_t)(uintptr_t)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  touchpad_mapping_t* mapping = scene_get_touchpad_mapping(
    scene_get_current_index(), s_editing_pad_index);
  if (!mapping) return;
  
  uint8_t num_ccs = mapping->action.params.cc.num_ccs;
  
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
  
  // Route based on action type
  if (mapping->action.type == ACTION_SEND_CC_HOLD) {
    // CC Hold: open submenu with CC/Press/Release
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_hold_slot_page_create);
  } else if (mapping->action.type == ACTION_SEND_CC_CYCLE) {
    // CC Cycle: open submenu with CC/Step values
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_cycle_slot_page_create);
  } else {
    // Regular CC: open CC number roller directly
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
  
  // Action selector (always first)
  const char* action_name = get_action_display_name(mapping->action.type);
  snprintf(s_action_label[buf], sizeof(s_action_label[buf]), "Action: %s", action_name);
  s_detail_items[item_count++] = (menu_item_t){s_action_label[buf], nav_to_action_type, NULL, true};
  
  // Show CC slots for CC actions
  if (mapping->action.type == ACTION_SEND_CC ||
      mapping->action.type == ACTION_SEND_CC_HOLD ||
      mapping->action.type == ACTION_SEND_CC_CYCLE) {
    
    // For CC Cycle, show Steps selector before the CC slots
    if (mapping->action.type == ACTION_SEND_CC_CYCLE) {
      uint8_t steps = mapping->action.params.cc.num_cycle_steps;
      if (steps < 2) steps = 2;
      snprintf(s_steps_label[buf], sizeof(s_steps_label[buf]), "Steps: %u", (unsigned)steps);
      s_detail_items[item_count++] = (menu_item_t){
        s_steps_label[buf], nav_to_cc_cycle_steps, NULL, true
      };
    }
    
    for (int i = 0; i < 4; i++) {
      // Use appropriate display function based on action type
      const char* slot_display;
      if (mapping->action.type == ACTION_SEND_CC_HOLD) {
        slot_display = get_cc_hold_slot_display(&mapping->action, (uint8_t)i);
      } else if (mapping->action.type == ACTION_SEND_CC_CYCLE) {
        slot_display = get_cc_cycle_slot_display(&mapping->action, (uint8_t)i);
      } else {
        slot_display = get_cc_slot_display(&mapping->action, (uint8_t)i);
      }
      if (strcmp(slot_display, "Inactive") == 0) {
        // Inactive slot: show slot number
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]), 
          "Slot %d: Inactive", i + 1);
      } else {
        // Active slot: just show "ControlName: Value"
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]), 
          "%s", slot_display);
      }
      s_detail_items[item_count++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // TODO: Add parameter UI for other action types (PC, Scene Set, Note, etc.)
  
  const char* title = get_pad_display_name(s_editing_pad_index);
  
  return menu_create_page(title, s_detail_items, item_count);
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
  
  return menu_create_page("Pads", s_pad_items, item_count);
}

// Cleanup function (call when leaving pads menu)
void menu_page_pads_cleanup(void) {
  free_cc_options();
  s_pending_control = NULL;
}
