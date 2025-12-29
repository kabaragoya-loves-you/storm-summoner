#include "action_config.h"
#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "action.h"
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

#define TAG "ACTION_CONFIG"
#define MAX_CYCLE_STEPS 8
#define MAX_RANDOMIZE_SLOTS 8

// ============================================================================
// Static State
// ============================================================================

static action_config_context_t* s_ctx = NULL;
static bool s_callback_in_progress = false;

// CC slot editing state
static uint8_t s_editing_cc_slot = 0;
static uint8_t s_editing_step = 0;
static uint8_t s_pending_cc_number = 0;
static const midi_control_t* s_pending_control = NULL;

// Preset/Tempo cycle step editing
static uint8_t s_editing_preset_step = 0;
static uint8_t s_editing_tempo_step = 0;
static uint8_t s_editing_tw_step = 0;

// Randomize slot editing
static uint8_t s_editing_randomize_slot = 0;

// Dynamic CC options from device
typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};

// Filtered CC options for randomize
typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} filtered_cc_options_t;

static filtered_cc_options_t s_randomize_cc_options = {0};

// Label buffers
#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

static char s_action_label[LABEL_BUFFER_SETS][48];
static char s_steps_label[LABEL_BUFFER_SETS][24];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_cc_hold_cc_label[LABEL_BUFFER_SETS][40];
static char s_cc_hold_press_label[LABEL_BUFFER_SETS][40];
static char s_cc_hold_release_label[LABEL_BUFFER_SETS][40];
static char s_cc_cycle_cc_label[LABEL_BUFFER_SETS][40];
static char s_cc_cycle_step_labels[LABEL_BUFFER_SETS][8][40];
static char s_preset_label[LABEL_BUFFER_SETS][32];
static char s_preset_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_preset_hold_release_label[LABEL_BUFFER_SETS][32];
static char s_preset_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_preset_cycle_step_labels[LABEL_BUFFER_SETS][8][32];
static char s_tempo_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_tempo_hold_release_label[LABEL_BUFFER_SETS][32];
static char s_tempo_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_tempo_cycle_step_labels[LABEL_BUFFER_SETS][8][32];
static char s_tw_mode_label[LABEL_BUFFER_SETS][32];
static char s_tw_hold_press_label[LABEL_BUFFER_SETS][32];
static char s_tw_hold_release_label[LABEL_BUFFER_SETS][32];
static char s_tw_cycle_steps_label[LABEL_BUFFER_SETS][24];
static char s_tw_cycle_step_labels[LABEL_BUFFER_SETS][8][32];
static char s_scene_label[LABEL_BUFFER_SETS][40];
static char s_tempo_label[LABEL_BUFFER_SETS][24];
static char s_note_label[LABEL_BUFFER_SETS][24];
static char s_randomize_slot_labels[LABEL_BUFFER_SETS][8][40];

// Menu items for submenu pages
#define MAX_DETAIL_ITEMS 12
static menu_item_t s_detail_items[MAX_DETAIL_ITEMS];
static menu_item_t s_cc_hold_items[3];
static menu_item_t s_cc_cycle_items[9];

// Note names
static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ============================================================================
// Action Types
// ============================================================================

static const action_type_t s_all_action_types[] = {
  ACTION_NONE,
  ACTION_CONTROL_CHANGE,
  ACTION_CONTROL_HOLD,
  ACTION_CONTROL_CYCLE,
  ACTION_PRESET_INC,
  ACTION_PRESET_DEC,
  ACTION_PRESET,
  ACTION_PRESET_HOLD,
  ACTION_PRESET_CYCLE,
  ACTION_SCENE_INC,
  ACTION_SCENE_DEC,
  ACTION_SCENE,
  ACTION_CONFIRM_PENDING,
  ACTION_PLAY,
  ACTION_STOP,
  ACTION_PAUSE,
  ACTION_RECORD,
  ACTION_TAP,
  ACTION_TAP_TEMPO,
  ACTION_SET_TEMPO,
  ACTION_TEMPO_INC,
  ACTION_TEMPO_DEC,
  ACTION_TEMPO_HOLD,
  ACTION_TEMPO_CYCLE,
  ACTION_NOTE,
  ACTION_RANDOMIZE,
  ACTION_RESET,
  ACTION_SUSTAIN,
  ACTION_SOSTENUTO,
  ACTION_TOUCHWHEEL_MODE,
  ACTION_TOUCHWHEEL_HOLD,
  ACTION_TOUCHWHEEL_CYCLE,
};
#define NUM_ALL_ACTION_TYPES (sizeof(s_all_action_types) / sizeof(s_all_action_types[0]))

static action_type_t s_filtered_action_types[NUM_ALL_ACTION_TYPES];
static size_t s_num_filtered_action_types = 0;

// ============================================================================
// Helper Functions
// ============================================================================

static int get_next_buffer_set(void) {
  int set = s_current_buffer_set;
  s_current_buffer_set = (s_current_buffer_set + 1) % LABEL_BUFFER_SETS;
  return set;
}

static void persist_scene_changes(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = scene_get_current_index();
    scene_save_to_flash(scene_index);
  }
}

// Public API for display name
const char* action_config_get_display_name(action_type_t type) {
  switch (type) {
    case ACTION_NONE: return "<None>";
    case ACTION_CONTROL_CHANGE: return "Control Change";
    case ACTION_CONTROL_HOLD: return "Control Hold";
    case ACTION_CONTROL_CYCLE: return "Control Cycle";
    case ACTION_PRESET_INC: return "Preset +1";
    case ACTION_PRESET_DEC: return "Preset -1";
    case ACTION_PRESET: return "Set Preset";
    case ACTION_PRESET_HOLD: return "Preset Hold";
    case ACTION_PRESET_CYCLE: return "Preset Cycle";
    case ACTION_SCENE_INC: return "Scene +1";
    case ACTION_SCENE_DEC: return "Scene -1";
    case ACTION_SCENE: return "Set Scene";
    case ACTION_PLAY: return "Play";
    case ACTION_STOP: return "Stop";
    case ACTION_PAUSE: return "Pause";
    case ACTION_RECORD: return "Record";
    case ACTION_TAP: return "Tap";
    case ACTION_TAP_TEMPO: return "Tap Tempo";
    case ACTION_SET_TEMPO: return "Set Tempo";
    case ACTION_TEMPO_INC: return "Tempo +1";
    case ACTION_TEMPO_DEC: return "Tempo -1";
    case ACTION_TEMPO_HOLD: return "Tempo Hold";
    case ACTION_TEMPO_CYCLE: return "Tempo Cycle";
    case ACTION_NOTE: return "Note";
    case ACTION_RANDOMIZE: return "Randomize";
    case ACTION_CONFIRM_PENDING: return "Confirm Pending";
    case ACTION_RESET: return "Reset";
    case ACTION_SUSTAIN: return "Sustain";
    case ACTION_SOSTENUTO: return "Sostenuto";
    case ACTION_TOUCHWHEEL_MODE: return "Touchwheel";
    case ACTION_TOUCHWHEEL_HOLD: return "Touchwheel Hold";
    case ACTION_TOUCHWHEEL_CYCLE: return "Touchwheel Cycle";
    default: return "Unknown";
  }
}

static bool is_action_visible(action_type_t type) {
  scene_mode_t scene_mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  tempo_clock_source_t clock_source = scene_get_clock_source(scene_get_current_index());
  
  // Preset actions only in single or advanced mode
  if (type == ACTION_PRESET_INC || type == ACTION_PRESET_DEC || 
      type == ACTION_PRESET || type == ACTION_PRESET_HOLD || type == ACTION_PRESET_CYCLE) {
    if (scene_mode != SCENE_MODE_SINGLE && scene_mode != SCENE_MODE_ADVANCED) return false;
  }
  
  // Preset Hold only available in immediate change mode
  if (type == ACTION_PRESET_HOLD && change_mode != CHANGE_MODE_IMMEDIATE) return false;
  
  // Scene actions only in preset_sync or advanced mode
  if (type == ACTION_SCENE_INC || type == ACTION_SCENE_DEC || type == ACTION_SCENE) {
    if (scene_mode != SCENE_MODE_PRESET_SYNC && scene_mode != SCENE_MODE_ADVANCED) return false;
  }
  
  // Confirm pending only in pending change mode
  if (type == ACTION_CONFIRM_PENDING && change_mode != CHANGE_MODE_PENDING) return false;
  
  // Tempo actions only with internal clock
  if (type == ACTION_TAP || type == ACTION_TAP_TEMPO || type == ACTION_SET_TEMPO ||
      type == ACTION_TEMPO_INC || type == ACTION_TEMPO_DEC ||
      type == ACTION_TEMPO_HOLD || type == ACTION_TEMPO_CYCLE) {
    if (clock_source != CLOCK_SOURCE_INTERNAL) return false;
  }
  
  return true;
}

static void build_filtered_action_types(void) {
  s_num_filtered_action_types = 0;
  for (size_t i = 0; i < NUM_ALL_ACTION_TYPES; i++) {
    if (is_action_visible(s_all_action_types[i])) {
      s_filtered_action_types[s_num_filtered_action_types++] = s_all_action_types[i];
    }
  }
}

static uint32_t action_type_to_roller_index(action_type_t type) {
  for (size_t i = 0; i < s_num_filtered_action_types; i++) {
    if (s_filtered_action_types[i] == type) return (uint32_t)i;
  }
  return 0;
}

// ============================================================================
// CC Options Loading
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
  
  uint16_t total = device->control_count + 1;
  s_cc_options.cc_numbers = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  size_t str_size = total * 28;
  s_cc_options.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_cc_options.cc_numbers || !s_cc_options.options_str) {
    ESP_LOGE(TAG, "Failed to allocate CC options");
    free_cc_options();
    return false;
  }
  
  strcpy(s_cc_options.options_str, "Inactive");
  s_cc_options.cc_numbers[0] = 0xFF;
  s_cc_options.count = 1;
  
  size_t pos = strlen("Inactive");
  for (uint16_t i = 0; i < device->control_count && (pos + 28) < str_size; i++) {
    const midi_control_t* ctrl = &device->controls[i];
    if (ctrl->type == MIDI_CONTROL_TYPE_CC) {
      s_cc_options.options_str[pos++] = '\n';
      const char* name = ctrl->name ? ctrl->name : "Unknown";
      size_t name_len = strlen(name);
      if (name_len > 24) name_len = 24;
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

static uint32_t cc_number_to_option_index(uint8_t cc_num) {
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    if (s_cc_options.cc_numbers[i] == cc_num) return i;
  }
  return 0;
}

// ============================================================================
// CC Slot Display Helpers
// ============================================================================

// Get display name for a CC slot: "ControlName: ValueName" or "ControlName: 64"
static const char* get_cc_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  if (slot >= action->params.control.num_ccs) return "Inactive";
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  uint8_t cc_val = action->params.control.values[slot];
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
  
  static char buf[40];
  const char* ctrl_name = (ctrl && ctrl->name) ? ctrl->name : NULL;
  const char* value_name = assets_get_discrete_name(device, cc_num, cc_val);
  
  if (ctrl_name) {
    if (value_name) {
      snprintf(buf, sizeof(buf), "%.16s: %.18s", ctrl_name, value_name);
    } else {
      snprintf(buf, sizeof(buf), "%.24s: %u", ctrl_name, (unsigned)cc_val);
    }
  } else {
    snprintf(buf, sizeof(buf), "CC%u: %u", (unsigned)cc_num, (unsigned)cc_val);
  }
  return buf;
}

// Get display name for a CC Hold slot: just the CC name
static const char* get_cc_hold_slot_display(action_t* action, uint8_t slot) {
  if (!action || slot >= 4) return "Inactive";
  if (slot >= action->params.control.num_ccs) return "Inactive";
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
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
static const char* get_value_display(uint8_t cc_num, uint8_t value) {
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
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
  if (slot >= action->params.control.num_ccs) return "Inactive";
  
  uint8_t cc_num = action->params.control.cc_numbers[slot];
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

// Get note display name from MIDI number
static void get_note_display_name(uint8_t midi_note, char* buf, size_t buf_size) {
  int octave = (midi_note / 12) - 1;
  int note_idx = midi_note % 12;
  snprintf(buf, buf_size, "%s%d", NOTE_NAMES[note_idx], octave);
}

// ============================================================================
// Public API
// ============================================================================

void action_config_init(void) {
  s_ctx = NULL;
  s_callback_in_progress = false;
}

action_config_context_t* action_config_get_context(void) {
  return s_ctx;
}

void action_config_start(action_config_context_t* ctx) {
  s_ctx = ctx;
  load_cc_options();
  const char* title = ctx->detail_title ? ctx->detail_title : "Action";
  menu_navigate_to(title, action_config_detail_page_create);
}

void action_config_cleanup(void) {
  free_cc_options();
  s_ctx = NULL;
  s_pending_control = NULL;
}

// ============================================================================
// Return Navigation
// ============================================================================

// Return to the action detail page (used by parameter rollers)
static void return_to_detail_page(uint8_t depth) {
  if (!s_ctx) {
    menu_navigate_back();
    return;
  }
  
  persist_scene_changes();
  
  const char* title = s_ctx->detail_title ? s_ctx->detail_title : "Action";
  menu_navigate_back_then_to(depth, title, action_config_detail_page_create);
}

// Return to the source page (used when backing out of detail page)
static void return_to_source(void) {
  if (!s_ctx) {
    menu_navigate_back();
    return;
  }
  
  if (s_ctx->on_complete && s_ctx->target_action) {
    s_ctx->on_complete(s_ctx, s_ctx->target_action);
  }
  
  persist_scene_changes();
  
  if (s_ctx->return_page) {
    menu_navigate_back_then_to(s_ctx->return_depth, s_ctx->source_title, s_ctx->return_page);
  } else {
    for (int i = 0; i < s_ctx->return_depth; i++) {
      menu_navigate_back();
    }
  }
}

// ============================================================================
// Action Type Roller
// ============================================================================

static void action_type_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action || selected_index >= s_num_filtered_action_types) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_type_t new_type = s_filtered_action_types[selected_index];
  action_t* action = s_ctx->target_action;
  
  // If changing action type, reset the action
  if (action->type != new_type) {
    memset(action, 0, sizeof(action_t));
    action->type = new_type;
    
    // Set defaults based on type
    if (new_type == ACTION_CONTROL_CYCLE) {
      action->params.control.num_cycle_steps = 2;
    }
    
    if (new_type == ACTION_PRESET) {
      uint8_t scene_index = scene_get_current_index();
      const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
      uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
      action->params.preset.program = index_base;
    }
    
    if (new_type == ACTION_SCENE) {
      action->params.target.number = scene_get_index_by_position(0);
    }
    
    if (new_type == ACTION_SET_TEMPO) {
      action->params.tempo.bpm = 120;
    }
    
    if (new_type == ACTION_NOTE) {
      action->params.note.note = 60;
      action->params.note.velocity = 100;
    }
    
    if (new_type == ACTION_PRESET_HOLD) {
      uint8_t scene_index = scene_get_current_index();
      const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
      uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
      action->params.preset_cycle.press_preset = index_base;
      action->params.preset_cycle.release_preset = index_base;
    }
    
    if (new_type == ACTION_PRESET_CYCLE) {
      uint8_t scene_index = scene_get_current_index();
      const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
      uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
      action->params.preset_cycle.num_presets = 2;
      action->params.preset_cycle.cycle_presets[0] = index_base;
      action->params.preset_cycle.cycle_presets[1] = index_base;
    }
    
    if (new_type == ACTION_TEMPO_HOLD) {
      action->params.tempo.press_bpm = 120;
      action->params.tempo.release_bpm = 120;
    }
    
    if (new_type == ACTION_TEMPO_CYCLE) {
      action->params.tempo.num_tempos = 2;
      action->params.tempo.cycle_tempos[0] = 120;
      action->params.tempo.cycle_tempos[1] = 120;
    }
    
    if (new_type == ACTION_TOUCHWHEEL_MODE) {
      action->params.tw_mode.mode = 0;
    }
    
    if (new_type == ACTION_TOUCHWHEEL_HOLD) {
      action->params.tw_mode.mode = 0;
      action->params.tw_mode.mode2 = 0;
    }
    
    if (new_type == ACTION_TOUCHWHEEL_CYCLE) {
      action->params.tw_mode.num_modes = 2;
      action->params.tw_mode.modes[0] = 0;
      action->params.tw_mode.modes[1] = 0;
    }
    
    ESP_LOGI(TAG, "Action type changed to: %s", action_config_get_display_name(new_type));
  }
  
  s_callback_in_progress = false;
  // Go back 2: pop roller AND old detail page, push fresh detail page
  return_to_detail_page(2);
}

lv_obj_t* action_config_type_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  build_filtered_action_types();
  
  static char options[512];
  options[0] = '\0';
  for (size_t i = 0; i < s_num_filtered_action_types; i++) {
    if (i > 0) strcat(options, "\n");
    strcat(options, action_config_get_display_name(s_filtered_action_types[i]));
  }
  
  uint32_t current_idx = action_type_to_roller_index(s_ctx->target_action->type);
  
  return menu_create_roller_page("Action", options, current_idx, action_type_confirm_cb, NULL);
}

// ============================================================================
// Forward Declarations
// ============================================================================

static lv_obj_t* cc_hold_slot_page_create(void);
static lv_obj_t* cc_cycle_slot_page_create(void);

// ============================================================================
// CC Value Roller (for ACTION_CONTROL_CHANGE)
// ============================================================================

static void cc_value_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_value;
  
  // Determine the actual value based on control type
  if (s_pending_control && s_pending_control->discrete_count > 0) {
    if (selected_index < s_pending_control->discrete_count) {
      cc_value = (uint8_t)s_pending_control->discrete_values[selected_index].value;
    } else {
      cc_value = 0;
    }
  } else if (s_pending_control) {
    cc_value = (uint8_t)(s_pending_control->min + selected_index);
  } else {
    cc_value = (uint8_t)selected_index;
  }
  
  action->params.control.cc_numbers[slot] = s_pending_cc_number;
  action->params.control.values[slot] = cc_value;
  
  if (slot >= action->params.control.num_ccs) {
    action->params.control.num_ccs = slot + 1;
  }
  
  ESP_LOGI(TAG, "CC slot %u set to CC%u=%u", (unsigned)(slot + 1),
    (unsigned)s_pending_cc_number, (unsigned)cc_value);
  
  s_pending_control = NULL;
  s_callback_in_progress = false;
  
  // Go back 3: pop value roller, number roller, old detail page
  return_to_detail_page(3);
}

static lv_obj_t* cc_value_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  s_pending_control = assets_get_control_by_cc(device, s_pending_cc_number);
  
  static char options[1024];
  options[0] = '\0';
  uint32_t current_idx = 0;
  uint8_t current_val = action->params.control.values[slot];
  
  if (s_pending_control && s_pending_control->discrete_count > 0) {
    for (int i = 0; i < s_pending_control->discrete_count; i++) {
      if (i > 0) strcat(options, "\n");
      const char* name = s_pending_control->discrete_values[i].name;
      if (name) {
        char truncated[28];
        strncpy(truncated, name, 27);
        truncated[27] = '\0';
        strcat(options, truncated);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)s_pending_control->discrete_values[i].value);
        strcat(options, num);
      }
      if (s_pending_control->discrete_values[i].value == current_val) {
        current_idx = (uint32_t)i;
      }
    }
  } else if (s_pending_control) {
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
    for (int i = 0; i <= 127; i++) {
      if (i > 0) strcat(options, "\n");
      char num[8];
      snprintf(num, sizeof(num), "%d", i);
      strcat(options, num);
    }
    current_idx = current_val;
  }
  
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
// CC Number Roller (for ACTION_CONTROL_CHANGE)
// ============================================================================

static void cc_number_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact
    uint8_t slot = s_editing_cc_slot;
    uint8_t num_ccs = action->params.control.num_ccs;
    
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        action->params.control.cc_numbers[i] = action->params.control.cc_numbers[i + 1];
        action->params.control.values[i] = action->params.control.values[i + 1];
        action->params.control.values2[i] = action->params.control.values2[i + 1];
      }
      action->params.control.cc_numbers[3] = 0;
      action->params.control.values[3] = 0;
      action->params.control.values2[3] = 0;
      
      if (num_ccs > 0) {
        action->params.control.num_ccs = num_ccs - 1;
      }
      
      ESP_LOGI(TAG, "CC slot %u cleared", (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    return_to_detail_page(2);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    s_pending_cc_number = s_cc_options.cc_numbers[selected_index];
  } else {
    s_pending_cc_number = 0;
  }
  
  s_callback_in_progress = false;
  menu_navigate_to("Value", cc_value_roller_create);
}

static lv_obj_t* cc_number_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      ESP_LOGW(TAG, "No device CC options");
      return menu_create_page_2line("Error", NULL, 0);
    }
  }
  
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < action->params.control.num_ccs) {
    uint8_t cc_num = action->params.control.cc_numbers[slot];
    current_idx = cc_number_to_option_index(cc_num);
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(slot + 1));
  
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx,
    cc_number_confirm_cb, NULL);
}

// ============================================================================
// CC Slot Navigation (routes based on action type)
// ============================================================================

static void nav_to_cc_slot(void* user_data) {
  if (!s_ctx || !s_ctx->target_action) return;
  
  uint8_t clicked_slot = (uint8_t)(uintptr_t)user_data;
  action_t* action = s_ctx->target_action;
  uint8_t num_ccs = action->params.control.num_ccs;
  
  // If clicking on an inactive slot, redirect to the next available slot
  s_editing_cc_slot = (clicked_slot >= num_ccs) ? num_ccs : clicked_slot;
  
  if (!s_cc_options.options_str) load_cc_options();
  
  static char title[24];
  
  if (action->type == ACTION_CONTROL_HOLD) {
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_hold_slot_page_create);
  } else if (action->type == ACTION_CONTROL_CYCLE) {
    snprintf(title, sizeof(title), "Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_cycle_slot_page_create);
  } else {
    snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(s_editing_cc_slot + 1));
    menu_navigate_to(title, cc_number_roller_create);
  }
}

// ============================================================================
// CC Hold Slot Submenu
// ============================================================================

// Forward declarations for CC Hold rollers
static lv_obj_t* cc_hold_cc_roller_create(void);
static lv_obj_t* cc_hold_press_roller_create(void);
static lv_obj_t* cc_hold_release_roller_create(void);

// --- CC Hold: CC Number Roller ---

static void cc_hold_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact
    uint8_t num_ccs = action->params.control.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        action->params.control.cc_numbers[i] = action->params.control.cc_numbers[i + 1];
        action->params.control.values[i] = action->params.control.values[i + 1];
        action->params.control.values2[i] = action->params.control.values2[i + 1];
      }
      action->params.control.cc_numbers[3] = 0;
      action->params.control.values[3] = 0;
      action->params.control.values2[3] = 0;
      if (num_ccs > 0) {
        action->params.control.num_ccs = num_ccs - 1;
      }
      ESP_LOGI(TAG, "CC Hold slot %u cleared", (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    // Go back 3: pop CC roller, slot submenu, old detail page
    return_to_detail_page(3);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    action->params.control.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= action->params.control.num_ccs) {
      action->params.control.values[slot] = 127;   // Default press value
      action->params.control.values2[slot] = 0;    // Default release value
      action->params.control.num_ccs = slot + 1;
    }
    
    ESP_LOGI(TAG, "CC Hold slot %u CC set to %u", (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Return to slot submenu
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_hold_slot_page_create);
}

static lv_obj_t* cc_hold_cc_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      return menu_create_page("Error", NULL, 0);
    }
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < action->params.control.num_ccs) {
    uint8_t cc_num = action->params.control.cc_numbers[slot];
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
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  
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
  
  action->params.control.values[slot] = press_val;
  
  ESP_LOGI(TAG, "CC Hold slot %u press value set to %u",
    (unsigned)(slot + 1), (unsigned)press_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_hold_slot_page_create);
}

static lv_obj_t* cc_hold_press_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  uint8_t current_val = action->params.control.values[slot];
  
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
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  
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
  
  action->params.control.values2[slot] = release_val;
  
  ESP_LOGI(TAG, "CC Hold slot %u release value set to %u",
    (unsigned)(slot + 1), (unsigned)release_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_hold_slot_page_create);
}

static lv_obj_t* cc_hold_release_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  uint8_t current_val = action->params.control.values2[slot];
  
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

// --- CC Hold Slot Page ---

static lv_obj_t* cc_hold_slot_page_create(void) {
  if (!s_ctx || !s_ctx->target_action) return menu_create_page("Error", NULL, 0);
  
  action_t* action = s_ctx->target_action;
  int buf = get_next_buffer_set();
  uint8_t slot = s_editing_cc_slot;
  
  // Get current values
  bool is_active = (slot < action->params.control.num_ccs);
  uint8_t cc_num = is_active ? action->params.control.cc_numbers[slot] : 0;
  uint8_t press_val = is_active ? action->params.control.values[slot] : 0;
  uint8_t release_val = is_active ? action->params.control.values2[slot] : 0;
  
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
    
    const char* press_disp = get_value_display(cc_num, press_val);
    snprintf(s_cc_hold_press_label[buf], sizeof(s_cc_hold_press_label[buf]), "Press: %s", press_disp);
    
    const char* release_disp = get_value_display(cc_num, release_val);
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
// CC Cycle Slot Submenu
// ============================================================================

// Forward declarations
static lv_obj_t* cc_cycle_cc_roller_create(void);
static lv_obj_t* cc_cycle_step_roller_create(void);

// --- CC Cycle: CC Number Roller ---

static void cc_cycle_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  
  if (selected_index == 0) {
    // "Inactive" selected - clear this slot and compact
    uint8_t num_ccs = action->params.control.num_ccs;
    if (slot < num_ccs) {
      for (int i = slot; i < 3; i++) {
        action->params.control.cc_numbers[i] = action->params.control.cc_numbers[i + 1];
        for (int s = 0; s < MAX_CYCLE_STEPS; s++) {
          action->params.control.cycle_values[i][s] = 
            action->params.control.cycle_values[i + 1][s];
        }
      }
      action->params.control.cc_numbers[3] = 0;
      memset(action->params.control.cycle_values[3], 0, MAX_CYCLE_STEPS);
      if (num_ccs > 0) {
        action->params.control.num_ccs = num_ccs - 1;
      }
      ESP_LOGI(TAG, "CC Cycle slot %u cleared", (unsigned)(slot + 1));
    }
    
    s_callback_in_progress = false;
    // Go back 3: pop CC roller, slot submenu, old detail page
    return_to_detail_page(3);
    return;
  }
  
  // CC selected from device list
  if (selected_index < s_cc_options.count) {
    uint8_t cc_num = s_cc_options.cc_numbers[selected_index];
    action->params.control.cc_numbers[slot] = cc_num;
    
    // If this is a new slot, set default values and increment num_ccs
    if (slot >= action->params.control.num_ccs) {
      // Default cycle values: 0, 127 for 2 steps
      uint8_t steps = action->params.control.num_cycle_steps;
      if (steps < 2) {
        steps = 2;
        action->params.control.num_cycle_steps = steps;
      }
      action->params.control.cycle_values[slot][0] = 0;
      action->params.control.cycle_values[slot][1] = 127;
      action->params.control.num_ccs = slot + 1;
    }
    
    ESP_LOGI(TAG, "CC Cycle slot %u CC set to %u", (unsigned)(slot + 1), (unsigned)cc_num);
  }
  
  s_callback_in_progress = false;
  
  // Return to slot submenu
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_cycle_slot_page_create);
}

static lv_obj_t* cc_cycle_cc_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    if (!load_cc_options()) {
      return menu_create_page("Error", NULL, 0);
    }
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < action->params.control.num_ccs) {
    uint8_t cc_num = action->params.control.cc_numbers[slot];
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
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t step = s_editing_step;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  
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
  
  action->params.control.cycle_values[slot][step] = step_val;
  
  ESP_LOGI(TAG, "CC Cycle slot %u step %u value set to %u",
    (unsigned)(slot + 1), (unsigned)(step + 1), (unsigned)step_val);
  
  s_callback_in_progress = false;
  
  static char title[24];
  snprintf(title, sizeof(title), "Slot %u", (unsigned)(slot + 1));
  menu_navigate_back_then_to(2, title, cc_cycle_slot_page_create);
}

static lv_obj_t* cc_cycle_step_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_cc_slot;
  uint8_t step = s_editing_step;
  uint8_t cc_num = action->params.control.cc_numbers[slot];
  uint8_t current_val = action->params.control.cycle_values[slot][step];
  
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

// --- CC Cycle Slot Page ---

static lv_obj_t* cc_cycle_slot_page_create(void) {
  if (!s_ctx || !s_ctx->target_action) return menu_create_page("Error", NULL, 0);
  
  action_t* action = s_ctx->target_action;
  int buf = get_next_buffer_set();
  uint8_t slot = s_editing_cc_slot;
  
  // Get current values
  bool is_active = (slot < action->params.control.num_ccs);
  uint8_t cc_num = is_active ? action->params.control.cc_numbers[slot] : 0;
  uint8_t num_steps = action->params.control.num_cycle_steps;
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
      uint8_t step_val = action->params.control.cycle_values[slot][i];
      const char* val_disp = get_value_display(cc_num, step_val);
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
// CC Cycle Steps Roller
// ============================================================================

static void cc_cycle_steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t new_steps = (uint8_t)(selected_index + 2);
  s_ctx->target_action->params.control.num_cycle_steps = new_steps;
  
  ESP_LOGI(TAG, "CC Cycle steps set to %u", (unsigned)new_steps);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* cc_cycle_steps_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[32];
  snprintf(options, sizeof(options), "2\n3\n4\n5\n6\n7\n8");
  
  uint8_t current_steps = s_ctx->target_action->params.control.num_cycle_steps;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx, cc_cycle_steps_confirm_cb, NULL);
}

static void nav_to_cc_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", cc_cycle_steps_roller_create);
}

// ============================================================================
// Preset Set Roller (for ACTION_PRESET)
// ============================================================================

static void preset_set_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  
  // Get device for index_base
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  // Convert 0-based roller index to PC value
  action->params.preset.program = index_base + (uint16_t)selected_index;
  
  ESP_LOGI(TAG, "Preset set to %u (display %u)",
    (unsigned)action->params.preset.program, (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* preset_set_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  
  // Get device for preset count
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t count = 128;
  uint16_t index_base = 0;
  if (device && device->pc_info) {
    count = device->pc_info->count;
    index_base = device->pc_info->index_base;
  }
  
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
  
  uint16_t current_program = action->params.preset.program;
  uint32_t current_idx = 0;
  if (current_program >= index_base) {
    current_idx = current_program - index_base;
    if (current_idx >= count) current_idx = 0;
  }
  
  return menu_create_roller_page("Preset", options, current_idx, preset_set_confirm_cb, NULL);
}

static void nav_to_preset_set(void* user_data) {
  (void)user_data;
  menu_navigate_to("Preset", preset_set_roller_create);
}

// ============================================================================
// Preset Hold Rollers (for ACTION_PRESET_HOLD)
// ============================================================================

static void preset_hold_press_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  action->params.preset_cycle.press_preset = index_base + (uint16_t)selected_index;
  
  ESP_LOGI(TAG, "Preset Hold press set to %u",
    (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* preset_hold_press_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t count = 128;
  uint16_t index_base = 0;
  if (device && device->pc_info) {
    count = device->pc_info->count;
    index_base = device->pc_info->index_base;
  }
  
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
  
  uint16_t current = action->params.preset_cycle.press_preset;
  uint32_t current_idx = 0;
  if (current >= index_base) {
    current_idx = current - index_base;
    if (current_idx >= count) current_idx = 0;
  }
  
  return menu_create_roller_page("Press", options, current_idx, preset_hold_press_confirm_cb, NULL);
}

static void nav_to_preset_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", preset_hold_press_roller_create);
}

static void preset_hold_release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  action->params.preset_cycle.release_preset = index_base + (uint16_t)selected_index;
  
  ESP_LOGI(TAG, "Preset Hold release set to %u",
    (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* preset_hold_release_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t count = 128;
  uint16_t index_base = 0;
  if (device && device->pc_info) {
    count = device->pc_info->count;
    index_base = device->pc_info->index_base;
  }
  
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
  
  uint16_t current = action->params.preset_cycle.release_preset;
  uint32_t current_idx = 0;
  if (current >= index_base) {
    current_idx = current - index_base;
    if (current_idx >= count) current_idx = 0;
  }
  
  return menu_create_roller_page("Release", options, current_idx, preset_hold_release_confirm_cb, NULL);
}

static void nav_to_preset_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", preset_hold_release_roller_create);
}

// ============================================================================
// Preset Cycle Rollers (for ACTION_PRESET_CYCLE)
// ============================================================================

static void preset_cycle_steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t new_steps = (uint8_t)(selected_index + 2);
  s_ctx->target_action->params.preset_cycle.num_presets = new_steps;
  
  ESP_LOGI(TAG, "Preset Cycle steps set to %u", (unsigned)new_steps);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* preset_cycle_steps_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[32];
  snprintf(options, sizeof(options), "2\n3\n4\n5\n6\n7\n8");
  
  uint8_t current_steps = s_ctx->target_action->params.preset_cycle.num_presets;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx, preset_cycle_steps_confirm_cb, NULL);
}

static void nav_to_preset_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", preset_cycle_steps_roller_create);
}

static void preset_cycle_step_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t step = s_editing_preset_step;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
  
  action->params.preset_cycle.cycle_presets[step] = index_base + (uint16_t)selected_index;
  
  ESP_LOGI(TAG, "Preset Cycle step %u set to %u",
    (unsigned)(step + 1), (unsigned)(selected_index + 1));
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* preset_cycle_step_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t step = s_editing_preset_step;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint16_t count = 128;
  uint16_t index_base = 0;
  if (device && device->pc_info) {
    count = device->pc_info->count;
    index_base = device->pc_info->index_base;
  }
  
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
  
  uint16_t current = action->params.preset_cycle.cycle_presets[step];
  uint32_t current_idx = 0;
  if (current >= index_base) {
    current_idx = current - index_base;
    if (current_idx >= count) current_idx = 0;
  }
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  return menu_create_roller_page(title, options, current_idx, preset_cycle_step_confirm_cb, NULL);
}

static void nav_to_preset_cycle_step(void* user_data) {
  s_editing_preset_step = (uint8_t)(uintptr_t)user_data;
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_preset_step + 1));
  menu_navigate_to(title, preset_cycle_step_roller_create);
}

// ============================================================================
// Set Tempo Roller (for ACTION_SET_TEMPO)
// ============================================================================

static void tempo_set_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // BPM range: 20-300
  uint16_t bpm = 20 + (uint16_t)selected_index;
  s_ctx->target_action->params.tempo.bpm = bpm;
  
  ESP_LOGI(TAG, "Tempo set to %u BPM", (unsigned)bpm);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tempo_set_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  // Build options: 20-300 BPM
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
  
  uint16_t current_bpm = s_ctx->target_action->params.tempo.bpm;
  if (current_bpm < 20) current_bpm = 120;
  if (current_bpm > 300) current_bpm = 120;
  uint32_t current_idx = current_bpm - 20;
  
  return menu_create_roller_page("Tempo", options, current_idx, tempo_set_confirm_cb, NULL);
}

static void nav_to_tempo_set(void* user_data) {
  (void)user_data;
  menu_navigate_to("Tempo", tempo_set_roller_create);
}

// ============================================================================
// Tempo Hold Rollers (for ACTION_TEMPO_HOLD)
// ============================================================================

static void tempo_hold_press_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint16_t bpm = 20 + (uint16_t)selected_index;
  s_ctx->target_action->params.tempo.press_bpm = bpm;
  
  ESP_LOGI(TAG, "Tempo Hold press set to %u BPM", (unsigned)bpm);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tempo_hold_press_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
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
  
  uint16_t current_bpm = s_ctx->target_action->params.tempo.press_bpm;
  if (current_bpm < 20) current_bpm = 120;
  if (current_bpm > 300) current_bpm = 120;
  uint32_t current_idx = current_bpm - 20;
  
  return menu_create_roller_page("Press", options, current_idx, tempo_hold_press_confirm_cb, NULL);
}

static void nav_to_tempo_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", tempo_hold_press_roller_create);
}

static void tempo_hold_release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint16_t bpm = 20 + (uint16_t)selected_index;
  s_ctx->target_action->params.tempo.release_bpm = bpm;
  
  ESP_LOGI(TAG, "Tempo Hold release set to %u BPM", (unsigned)bpm);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tempo_hold_release_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
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
  
  uint16_t current_bpm = s_ctx->target_action->params.tempo.release_bpm;
  if (current_bpm < 20) current_bpm = 120;
  if (current_bpm > 300) current_bpm = 120;
  uint32_t current_idx = current_bpm - 20;
  
  return menu_create_roller_page("Release", options, current_idx, tempo_hold_release_confirm_cb, NULL);
}

static void nav_to_tempo_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", tempo_hold_release_roller_create);
}

// ============================================================================
// Tempo Cycle Rollers (for ACTION_TEMPO_CYCLE)
// ============================================================================

static void tempo_cycle_steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t new_steps = (uint8_t)(selected_index + 2);
  s_ctx->target_action->params.tempo.num_tempos = new_steps;
  
  ESP_LOGI(TAG, "Tempo Cycle steps set to %u", (unsigned)new_steps);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tempo_cycle_steps_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[32];
  snprintf(options, sizeof(options), "2\n3\n4\n5\n6\n7\n8");
  
  uint8_t current_steps = s_ctx->target_action->params.tempo.num_tempos;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx, tempo_cycle_steps_confirm_cb, NULL);
}

static void nav_to_tempo_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", tempo_cycle_steps_roller_create);
}

static void tempo_cycle_step_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t step = s_editing_tempo_step;
  uint16_t bpm = 20 + (uint16_t)selected_index;
  
  s_ctx->target_action->params.tempo.cycle_tempos[step] = bpm;
  
  ESP_LOGI(TAG, "Tempo Cycle step %u set to %u BPM",
    (unsigned)(step + 1), (unsigned)bpm);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tempo_cycle_step_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  uint8_t step = s_editing_tempo_step;
  
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
  
  uint16_t current_bpm = s_ctx->target_action->params.tempo.cycle_tempos[step];
  if (current_bpm < 20) current_bpm = 120;
  if (current_bpm > 300) current_bpm = 120;
  uint32_t current_idx = current_bpm - 20;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  return menu_create_roller_page(title, options, current_idx, tempo_cycle_step_confirm_cb, NULL);
}

static void nav_to_tempo_cycle_step(void* user_data) {
  s_editing_tempo_step = (uint8_t)(uintptr_t)user_data;
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_tempo_step + 1));
  menu_navigate_to(title, tempo_cycle_step_roller_create);
}

// ============================================================================
// Scene Set Roller (for ACTION_SCENE)
// ============================================================================

static void scene_set_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t scene_index = scene_get_index_by_position((uint16_t)selected_index);
  s_ctx->target_action->params.target.number = scene_index;
  
  const char* scene_name = scene_get_name_by_position((uint16_t)selected_index);
  ESP_LOGI(TAG, "Scene Set target set to %s (index %u)",
    scene_name ? scene_name : "?", (unsigned)scene_index);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* scene_set_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  uint16_t count = scene_get_count();
  if (count == 0) {
    return menu_create_page("No Scenes", NULL, 0);
  }
  
  static char options[1024];
  options[0] = '\0';
  char* pos = options;
  size_t remaining = sizeof(options);
  
  uint32_t current_idx = 0;
  uint8_t target_scene = s_ctx->target_action->params.target.number;
  
  for (uint16_t i = 0; i < count && remaining > 40; i++) {
    const char* name = scene_get_name_by_position(i);
    if (!name) name = "?";
    
    int written = snprintf(pos, remaining, "%s%s", i > 0 ? "\n" : "", name);
    if (written > 0 && (size_t)written < remaining) {
      pos += written;
      remaining -= written;
    }
    
    if (scene_get_index_by_position(i) == target_scene) {
      current_idx = i;
    }
  }
  
  return menu_create_roller_page("Scene", options, current_idx, scene_set_confirm_cb, NULL);
}

static void nav_to_scene_set(void* user_data) {
  (void)user_data;
  menu_navigate_to("Scene", scene_set_roller_create);
}

// ============================================================================
// Note Roller (for ACTION_NOTE)
// ============================================================================

static void note_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Convert roller index to MIDI note (C2=36 is index 0)
  uint8_t midi_note = 36 + selected_idx;
  if (midi_note > 96) midi_note = 96;
  
  s_ctx->target_action->params.note.note = midi_note;
  
  char note_name[8];
  get_note_display_name(midi_note, note_name, sizeof(note_name));
  ESP_LOGI(TAG, "Note set to %s (MIDI %u)", note_name, (unsigned)midi_note);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* note_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
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
  
  uint8_t current_note = s_ctx->target_action->params.note.note;
  if (current_note < 36) current_note = 60;
  if (current_note > 96) current_note = 96;
  uint32_t current_idx = current_note - 36;
  
  return menu_create_roller_page("Note", options, current_idx, note_confirm_cb, NULL);
}

static void nav_to_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Note", note_roller_create);
}

// ============================================================================
// Touchwheel Mode Rollers
// ============================================================================

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

static void tw_mode_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    s_ctx->target_action->params.tw_mode.mode = (uint8_t)selected_idx;
    ESP_LOGI(TAG, "Touchwheel mode set to %s", touchwheel_get_mode_name(selected_idx));
  }
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tw_mode_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = s_ctx->target_action->params.tw_mode.mode;
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  return menu_create_roller_page("Mode", options, current, tw_mode_confirm_cb, NULL);
}

static void nav_to_tw_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", tw_mode_roller_create);
}

// ============================================================================
// Touchwheel Hold Rollers (for ACTION_TOUCHWHEEL_HOLD)
// ============================================================================

static void tw_hold_press_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    s_ctx->target_action->params.tw_mode.mode = (uint8_t)selected_idx;
    ESP_LOGI(TAG, "TW Hold press set to %s", touchwheel_get_mode_name(selected_idx));
  }
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tw_hold_press_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = s_ctx->target_action->params.tw_mode.mode;
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  return menu_create_roller_page("Press", options, current, tw_hold_press_confirm_cb, NULL);
}

static void nav_to_tw_hold_press(void* user_data) {
  (void)user_data;
  menu_navigate_to("Press", tw_hold_press_roller_create);
}

static void tw_hold_release_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    s_ctx->target_action->params.tw_mode.mode2 = (uint8_t)selected_idx;
    ESP_LOGI(TAG, "TW Hold release set to %s", touchwheel_get_mode_name(selected_idx));
  }
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tw_hold_release_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = s_ctx->target_action->params.tw_mode.mode2;
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  return menu_create_roller_page("Release", options, current, tw_hold_release_confirm_cb, NULL);
}

static void nav_to_tw_hold_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", tw_hold_release_roller_create);
}

// ============================================================================
// Touchwheel Cycle Rollers (for ACTION_TOUCHWHEEL_CYCLE)
// ============================================================================

static void tw_cycle_steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t new_steps = (uint8_t)(selected_index + 2);
  s_ctx->target_action->params.tw_mode.num_modes = new_steps;
  
  ESP_LOGI(TAG, "TW Cycle steps set to %u", (unsigned)new_steps);
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tw_cycle_steps_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  static char options[32];
  snprintf(options, sizeof(options), "2\n3\n4\n5\n6\n7\n8");
  
  uint8_t current_steps = s_ctx->target_action->params.tw_mode.num_modes;
  if (current_steps < 2) current_steps = 2;
  if (current_steps > 8) current_steps = 8;
  uint32_t current_idx = current_steps - 2;
  
  return menu_create_roller_page("Steps", options, current_idx, tw_cycle_steps_confirm_cb, NULL);
}

static void nav_to_tw_cycle_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", tw_cycle_steps_roller_create);
}

static void tw_cycle_step_confirm_cb(uint32_t selected_idx, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t step = s_editing_tw_step;
  
  if (selected_idx < NUM_TOUCHWHEEL_USER_MODES) {
    s_ctx->target_action->params.tw_mode.modes[step] = (uint8_t)selected_idx;
    ESP_LOGI(TAG, "TW Cycle step %u set to %s",
      (unsigned)(step + 1), touchwheel_get_mode_name(selected_idx));
  }
  
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* tw_cycle_step_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  uint8_t step = s_editing_tw_step;
  
  static char options[512];
  build_tw_mode_options(options, sizeof(options));
  
  uint8_t current = s_ctx->target_action->params.tw_mode.modes[step];
  if (current >= NUM_TOUCHWHEEL_USER_MODES) current = 0;
  
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(step + 1));
  
  return menu_create_roller_page(title, options, current, tw_cycle_step_confirm_cb, NULL);
}

static void nav_to_tw_cycle_step(void* user_data) {
  s_editing_tw_step = (uint8_t)(uintptr_t)user_data;
  static char title[24];
  snprintf(title, sizeof(title), "Step %u", (unsigned)(s_editing_tw_step + 1));
  menu_navigate_to(title, tw_cycle_step_roller_create);
}

// ============================================================================
// Randomize Rollers (for ACTION_RANDOMIZE)
// ============================================================================

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

static bool build_randomize_cc_options(action_t* action, uint8_t editing_slot) {
  free_randomize_cc_options();
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  if (!device || device->control_count == 0) {
    return false;
  }
  
  // Build set of already-used CCs (excluding current slot)
  uint8_t used_ccs[MAX_RANDOMIZE_SLOTS];
  int num_used = 0;
  for (int i = 0; i < action->params.randomize.num_ccs && i < MAX_RANDOMIZE_SLOTS; i++) {
    if (i != editing_slot) {
      used_ccs[num_used++] = action->params.randomize.cc_numbers[i];
    }
  }
  
  // Count available CCs
  uint16_t available = 0;
  for (uint16_t i = 0; i < device->control_count; i++) {
    if (device->controls[i].type == MIDI_CONTROL_TYPE_CC) {
      bool is_used = false;
      for (int j = 0; j < num_used; j++) {
        if (used_ccs[j] == device->controls[i].id) {
          is_used = true;
          break;
        }
      }
      if (!is_used) available++;
    }
  }
  
  uint16_t total = available + 1;  // +1 for "Inactive"
  s_randomize_cc_options.cc_numbers = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  size_t str_size = total * 28;
  s_randomize_cc_options.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_randomize_cc_options.cc_numbers || !s_randomize_cc_options.options_str) {
    free_randomize_cc_options();
    return false;
  }
  
  strcpy(s_randomize_cc_options.options_str, "Inactive");
  s_randomize_cc_options.cc_numbers[0] = 0xFF;
  s_randomize_cc_options.count = 1;
  
  size_t pos = strlen("Inactive");
  for (uint16_t i = 0; i < device->control_count && (pos + 28) < str_size; i++) {
    const midi_control_t* ctrl = &device->controls[i];
    if (ctrl->type != MIDI_CONTROL_TYPE_CC) continue;
    
    bool is_used = false;
    for (int j = 0; j < num_used; j++) {
      if (used_ccs[j] == ctrl->id) {
        is_used = true;
        break;
      }
    }
    if (is_used) continue;
    
    s_randomize_cc_options.options_str[pos++] = '\n';
    const char* name = ctrl->name ? ctrl->name : "Unknown";
    size_t name_len = strlen(name);
    if (name_len > 24) name_len = 24;
    memcpy(s_randomize_cc_options.options_str + pos, name, name_len);
    pos += name_len;
    s_randomize_cc_options.options_str[pos] = '\0';
    s_randomize_cc_options.cc_numbers[s_randomize_cc_options.count] = (uint8_t)ctrl->id;
    s_randomize_cc_options.count++;
  }
  
  return true;
}

static void randomize_cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  if (!s_ctx || !s_ctx->target_action) {
    s_callback_in_progress = false;
    free_randomize_cc_options();
    menu_navigate_back();
    return;
  }
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_randomize_slot;
  uint8_t num_ccs = action->params.randomize.num_ccs;
  
  if (selected_index == 0) {
    // "Inactive" - clear slot and compact
    if (slot < num_ccs) {
      for (int i = slot; i < MAX_RANDOMIZE_SLOTS - 1; i++) {
        action->params.randomize.cc_numbers[i] = action->params.randomize.cc_numbers[i + 1];
      }
      action->params.randomize.cc_numbers[MAX_RANDOMIZE_SLOTS - 1] = 0;
      if (num_ccs > 0) {
        action->params.randomize.num_ccs = num_ccs - 1;
      }
      ESP_LOGI(TAG, "Randomize slot %u cleared", (unsigned)slot);
    }
  } else if (selected_index < s_randomize_cc_options.count) {
    uint8_t cc_num = s_randomize_cc_options.cc_numbers[selected_index];
    action->params.randomize.cc_numbers[slot] = cc_num;
    
    if (slot >= num_ccs && slot < MAX_RANDOMIZE_SLOTS) {
      action->params.randomize.num_ccs = slot + 1;
    }
    
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    const char* cc_name = assets_get_cc_name(device, cc_num);
    ESP_LOGI(TAG, "Randomize slot %u set to CC%u (%s)",
      (unsigned)slot, (unsigned)cc_num, cc_name ? cc_name : "?");
  }
  
  free_randomize_cc_options();
  s_callback_in_progress = false;
  return_to_detail_page(2);
}

static lv_obj_t* randomize_cc_roller_create(void) {
  if (!s_ctx || !s_ctx->target_action) return NULL;
  
  action_t* action = s_ctx->target_action;
  uint8_t slot = s_editing_randomize_slot;
  
  if (!build_randomize_cc_options(action, slot)) {
    return menu_create_page("Error", NULL, 0);
  }
  
  uint32_t current_idx = 0;
  uint8_t num_ccs = action->params.randomize.num_ccs;
  if (slot < num_ccs) {
    uint8_t cc_num = action->params.randomize.cc_numbers[slot];
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
  if (!s_ctx || !s_ctx->target_action) return;
  
  uint8_t clicked_slot = (uint8_t)(uintptr_t)user_data;
  action_t* action = s_ctx->target_action;
  uint8_t num_ccs = action->params.randomize.num_ccs;
  
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
// Navigation Helpers
// ============================================================================

static void nav_to_action_type(void* user_data) {
  (void)user_data;
  menu_navigate_to("Action", action_config_type_roller_create);
}

// Back handler for detail page - returns to source
static bool detail_page_handle_back(void) {
  menu_set_custom_back_handler(NULL);
  return_to_source();
  return true;
}

// ============================================================================
// Action Detail Page (stub - parameters will be added in subsequent phases)
// ============================================================================

lv_obj_t* action_config_detail_page_create(void) {
  if (!s_ctx || !s_ctx->target_action) {
    ESP_LOGW(TAG, "No action config context");
    return menu_create_page_2line("Error", NULL, 0);
  }
  
  action_t* action = s_ctx->target_action;
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  // Action type selector (always first) - 2-line format
  const char* action_name = action_config_get_display_name(action->type);
  snprintf(s_action_label[buf], sizeof(s_action_label[buf]), "Action\n%s", action_name);
  s_detail_items[item_count++] = (menu_item_t){s_action_label[buf], nav_to_action_type, NULL, true};
  
  // CC actions: show CC slots
  if (action->type == ACTION_CONTROL_CHANGE ||
      action->type == ACTION_CONTROL_HOLD ||
      action->type == ACTION_CONTROL_CYCLE) {
    
    // For CC Cycle, show Steps selector first
    if (action->type == ACTION_CONTROL_CYCLE) {
      uint8_t steps = action->params.control.num_cycle_steps;
      if (steps < 2) steps = 2;
      snprintf(s_steps_label[buf], sizeof(s_steps_label[buf]), "Steps\n%u", (unsigned)steps);
      s_detail_items[item_count++] = (menu_item_t){
        s_steps_label[buf], nav_to_cc_cycle_steps, NULL, true
      };
    }
    
    // CC slots
    for (int i = 0; i < 4; i++) {
      const char* slot_display;
      if (action->type == ACTION_CONTROL_HOLD) {
        slot_display = get_cc_hold_slot_display(action, (uint8_t)i);
      } else if (action->type == ACTION_CONTROL_CYCLE) {
        slot_display = get_cc_cycle_slot_display(action, (uint8_t)i);
      } else {
        slot_display = get_cc_slot_display(action, (uint8_t)i);
      }
      
      if (strcmp(slot_display, "Inactive") == 0) {
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
          "Slot %d\nInactive", i + 1);
      } else {
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
          "Slot %d\n%s", i + 1, slot_display);
      }
      s_detail_items[item_count++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Preset selector for Program Set
  if (action->type == ACTION_PRESET) {
    uint16_t program = action->params.preset.program;
    
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    uint16_t display_num = program - index_base + 1;
    snprintf(s_preset_label[buf], sizeof(s_preset_label[buf]), "Preset\n%u", (unsigned)display_num);
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_label[buf], nav_to_preset_set, NULL, true
    };
  }
  
  // Show Preset Hold items
  if (action->type == ACTION_PRESET_HOLD) {
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    uint16_t press = action->params.preset_cycle.press_preset;
    uint16_t release = action->params.preset_cycle.release_preset;
    
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
  
  // Show Preset Cycle items
  if (action->type == ACTION_PRESET_CYCLE) {
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    uint16_t index_base = (device && device->pc_info) ? device->pc_info->index_base : 0;
    
    uint8_t num_steps = action->params.preset_cycle.num_presets;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    snprintf(s_preset_cycle_steps_label[buf], sizeof(s_preset_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_preset_cycle_steps_label[buf], nav_to_preset_cycle_steps, NULL, true
    };
    
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint16_t preset = action->params.preset_cycle.cycle_presets[i];
      snprintf(s_preset_cycle_step_labels[buf][i], sizeof(s_preset_cycle_step_labels[buf][i]),
        "Step %d\n%u", i + 1, (unsigned)(preset - index_base + 1));
      s_detail_items[item_count++] = (menu_item_t){
        s_preset_cycle_step_labels[buf][i], nav_to_preset_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Set Tempo selector
  if (action->type == ACTION_SET_TEMPO) {
    uint16_t bpm = action->params.tempo.bpm;
    if (bpm < 20 || bpm > 300) bpm = 120;
    
    snprintf(s_tempo_label[buf], sizeof(s_tempo_label[buf]), "Tempo\n%u BPM", (unsigned)bpm);
    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_label[buf], nav_to_tempo_set, NULL, true
    };
  }
  
  // Show Tempo Hold items
  if (action->type == ACTION_TEMPO_HOLD) {
    uint16_t press = action->params.tempo.press_bpm;
    uint16_t release = action->params.tempo.release_bpm;
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
  
  // Show Tempo Cycle items
  if (action->type == ACTION_TEMPO_CYCLE) {
    uint8_t num_steps = action->params.tempo.num_tempos;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    snprintf(s_tempo_cycle_steps_label[buf], sizeof(s_tempo_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_tempo_cycle_steps_label[buf], nav_to_tempo_cycle_steps, NULL, true
    };
    
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint16_t bpm = action->params.tempo.cycle_tempos[i];
      if (bpm < 20 || bpm > 300) bpm = 120;
      snprintf(s_tempo_cycle_step_labels[buf][i], sizeof(s_tempo_cycle_step_labels[buf][i]),
        "Step %d\n%u BPM", i + 1, (unsigned)bpm);
      s_detail_items[item_count++] = (menu_item_t){
        s_tempo_cycle_step_labels[buf][i], nav_to_tempo_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Scene selector for Scene Set
  if (action->type == ACTION_SCENE) {
    uint8_t target = action->params.target.number;
    
    const char* target_name = NULL;
    uint16_t count = scene_get_count();
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
  
  // Show Note selector
  if (action->type == ACTION_NOTE) {
    uint8_t midi_note = action->params.note.note;
    if (midi_note < 36) midi_note = 60;
    if (midi_note > 96) midi_note = 96;
    
    char note_name[8];
    get_note_display_name(midi_note, note_name, sizeof(note_name));
    snprintf(s_note_label[buf], sizeof(s_note_label[buf]), "Note\n%s", note_name);
    s_detail_items[item_count++] = (menu_item_t){
      s_note_label[buf], nav_to_note, NULL, true
    };
  }
  
  // Show Touchwheel Mode selector
  if (action->type == ACTION_TOUCHWHEEL_MODE) {
    uint8_t mode_idx = action->params.tw_mode.mode;
    if (mode_idx >= NUM_TOUCHWHEEL_USER_MODES) mode_idx = 0;
    
    snprintf(s_tw_mode_label[buf], sizeof(s_tw_mode_label[buf]),
      "Mode\n%s", touchwheel_get_mode_name(mode_idx));
    s_detail_items[item_count++] = (menu_item_t){
      s_tw_mode_label[buf], nav_to_tw_mode, NULL, true
    };
  }
  
  // Show Touchwheel Hold items
  if (action->type == ACTION_TOUCHWHEEL_HOLD) {
    uint8_t press_idx = action->params.tw_mode.mode;
    uint8_t release_idx = action->params.tw_mode.mode2;
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
  
  // Show Touchwheel Cycle items
  if (action->type == ACTION_TOUCHWHEEL_CYCLE) {
    uint8_t num_steps = action->params.tw_mode.num_modes;
    if (num_steps < 2) num_steps = 2;
    if (num_steps > 8) num_steps = 8;
    
    snprintf(s_tw_cycle_steps_label[buf], sizeof(s_tw_cycle_steps_label[buf]),
      "Steps\n%u", (unsigned)num_steps);
    s_detail_items[item_count++] = (menu_item_t){
      s_tw_cycle_steps_label[buf], nav_to_tw_cycle_steps, NULL, true
    };
    
    for (int i = 0; i < num_steps && item_count < MAX_DETAIL_ITEMS; i++) {
      uint8_t mode_idx = action->params.tw_mode.modes[i];
      if (mode_idx >= NUM_TOUCHWHEEL_USER_MODES) mode_idx = 0;
      snprintf(s_tw_cycle_step_labels[buf][i], sizeof(s_tw_cycle_step_labels[buf][i]),
        "Step %d\n%s", i + 1, touchwheel_get_mode_name(mode_idx));
      s_detail_items[item_count++] = (menu_item_t){
        s_tw_cycle_step_labels[buf][i], nav_to_tw_cycle_step, (void*)(uintptr_t)i, true
      };
    }
  }
  
  // Show Randomize slots
  if (action->type == ACTION_RANDOMIZE) {
    uint8_t num_ccs = action->params.randomize.num_ccs;
    uint8_t scene_index = scene_get_current_index();
    const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
    
    // Count available CC controls from device
    int max_slots = 0;
    if (device) {
      for (uint16_t i = 0; i < device->control_count && max_slots < MAX_RANDOMIZE_SLOTS; i++) {
        if (device->controls[i].type == MIDI_CONTROL_TYPE_CC) {
          max_slots++;
        }
      }
    }
    if (max_slots > MAX_RANDOMIZE_SLOTS) max_slots = MAX_RANDOMIZE_SLOTS;
    
    // Show slots: active slots + 1 inactive slot (if there's room)
    int slots_to_show = num_ccs + 1;
    if (slots_to_show > max_slots) slots_to_show = max_slots;
    
    for (int i = 0; i < slots_to_show && item_count < MAX_DETAIL_ITEMS; i++) {
      if (i < num_ccs) {
        uint8_t cc_num = action->params.randomize.cc_numbers[i];
        const char* cc_name = assets_get_cc_name(device, cc_num);
        if (cc_name && strcmp(cc_name, "Undefined") != 0) {
          snprintf(s_randomize_slot_labels[buf][i],
            sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\n%s", i + 1, cc_name);
        } else {
          snprintf(s_randomize_slot_labels[buf][i],
            sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\nCC %u", i + 1, (unsigned)cc_num);
        }
      } else {
        snprintf(s_randomize_slot_labels[buf][i],
          sizeof(s_randomize_slot_labels[buf][i]), "Slot %d\nInactive", i + 1);
      }
      s_detail_items[item_count++] = (menu_item_t){
        s_randomize_slot_labels[buf][i], nav_to_randomize_slot, (void*)(uintptr_t)i, true
      };
    }
  }
  
  const char* title = s_ctx->detail_title ? s_ctx->detail_title : "Action";
  
  menu_set_custom_back_handler(detail_page_handle_back);
  
  return menu_create_page_2line(title, s_detail_items, item_count);
}

