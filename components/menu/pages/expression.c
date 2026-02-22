#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "expression.h"
#include "scene.h"
#include "cv.h"
#include "action.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "lfo.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_EXPRESSION"

// Forward declarations
lv_obj_t* menu_page_expression_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_EXPR_ITEMS 12
static menu_item_t s_expr_items[MAX_EXPR_ITEMS];

static char s_mode_label[LABEL_BUFFER_SETS][32];
static char s_output_label[LABEL_BUFFER_SETS][32];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_polarity_label[LABEL_BUFFER_SETS][32];
static char s_curve_label[LABEL_BUFFER_SETS][32];
static char s_base_note_label[LABEL_BUFFER_SETS][32];
static char s_range_label[LABEL_BUFFER_SETS][32];
static char s_velocity_mode_label[LABEL_BUFFER_SETS][32];
static char s_velocity_label[LABEL_BUFFER_SETS][32];
static char s_action_label[LABEL_BUFFER_SETS][48];
static char s_lfo_target_label[LABEL_BUFFER_SETS][32];

// CC options from device
typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};
static uint8_t s_editing_cc_slot = 0;
static bool s_callback_in_progress = false;

// Action config context for Switch mode
static action_config_context_t s_switch_action_ctx;

// Note names
static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

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

static void get_note_name(uint8_t midi_note, char* buf, size_t buf_size) {
  int octave = (midi_note / 12) - 1;
  int note_idx = midi_note % 12;
  snprintf(buf, buf_size, "%s%d", NOTE_NAMES[note_idx], octave);
}

static const char* get_mode_display_name(expression_mode_t mode) {
  switch (mode) {
    case EXPRESSION_MODE_NONE: return "<None>";
    case EXPRESSION_MODE_PEDAL: return "Expression";
    case EXPRESSION_MODE_SUSTAIN: return "Sustain";
    case EXPRESSION_MODE_SOSTENUTO: return "Sostenuto";
    case EXPRESSION_MODE_SWITCH: return "Switch";
    case EXPRESSION_MODE_GATE: return "Gate";
    default: return "Unknown";
  }
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
  
  if (!device || device->control_count == 0) return false;
  
  uint16_t total = device->control_count + 1;
  s_cc_options.cc_numbers = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  size_t str_size = total * 28;
  s_cc_options.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);
  
  if (!s_cc_options.cc_numbers || !s_cc_options.options_str) {
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
  
  return true;
}

static uint32_t cc_number_to_option_index(uint8_t cc_num) {
  for (uint16_t i = 1; i < s_cc_options.count; i++) {
    if (s_cc_options.cc_numbers[i] == cc_num) return i;
  }
  return 0;
}

// ============================================================================
// Mode Roller
// ============================================================================

static void mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Map index to mode (Gate is not in the list, None is at index 0)
  expression_mode_t modes[] = {
    EXPRESSION_MODE_NONE,
    EXPRESSION_MODE_PEDAL,
    EXPRESSION_MODE_SUSTAIN,
    EXPRESSION_MODE_SOSTENUTO,
    EXPRESSION_MODE_SWITCH
  };
  
  if (selected_index >= 5) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  expression_mode_t new_mode = modes[selected_index];
  uint8_t scene_index = scene_get_current_index();
  
  esp_err_t ret = scene_set_expression_mode(scene_index, new_mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Expression mode set to: %s", get_mode_display_name(new_mode));
  } else {
    ESP_LOGW(TAG, "Failed to set expression mode: %s", esp_err_to_name(ret));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* mode_roller_create(void) {
  static const char* options = "<None>\nExpression\nSustain\nSostenuto\nSwitch";
  
  uint8_t scene_index = scene_get_current_index();
  expression_mode_t current = scene_get_expression_mode(scene_index);
  
  uint32_t current_idx = 0;
  switch (current) {
    case EXPRESSION_MODE_NONE: current_idx = 0; break;
    case EXPRESSION_MODE_PEDAL: current_idx = 1; break;
    case EXPRESSION_MODE_SUSTAIN: current_idx = 2; break;
    case EXPRESSION_MODE_SOSTENUTO: current_idx = 3; break;
    case EXPRESSION_MODE_SWITCH: current_idx = 4; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Mode", options, current_idx, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Output Type Roller (CC vs Notes)
// ============================================================================

static void output_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Map roller index to output type
  // 0=CC, 1=Note, 2=LFO Rate, 3=LFO Depth
  switch (selected_index) {
    case 0: scene->expression.output_type = OUTPUT_TYPE_CC; break;
    case 1: scene->expression.output_type = OUTPUT_TYPE_NOTE; break;
    case 2: scene->expression.output_type = OUTPUT_TYPE_LFO_RATE; break;
    case 3: scene->expression.output_type = OUTPUT_TYPE_LFO_DEPTH; break;
    default: scene->expression.output_type = OUTPUT_TYPE_CC; break;
  }
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Expression output set to type %d", (int)selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* output_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->expression.output_type) {
    case OUTPUT_TYPE_CC: current = 0; break;
    case OUTPUT_TYPE_NOTE: current = 1; break;
    case OUTPUT_TYPE_LFO_RATE: current = 2; break;
    case OUTPUT_TYPE_LFO_DEPTH: current = 3; break;
    default: current = 0; break;
  }
  return menu_create_roller_page("Output", "Control Change\nNotes\nLFO Rate\nLFO Depth",
    current, output_confirm_cb, NULL);
}

static void nav_to_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Output", output_roller_create);
}

// ============================================================================
// LFO Target Roller
// ============================================================================

static const char* lfo_target_to_string(lfo_target_t target) {
  switch (target) {
    case LFO_TARGET_LFO1: return "LFO1";
    case LFO_TARGET_LFO2: return "LFO2";
    case LFO_TARGET_BOTH: return "Both";
    default: return "Both";
  }
}

static void lfo_target_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.lfo_target = (lfo_target_t)selected_index;
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (uint32_t)scene->expression.lfo_target;
  return menu_create_roller_page("LFO Target", "LFO1\nLFO2\nBoth", current,
    lfo_target_confirm_cb, NULL);
}

static void nav_to_lfo_target(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO Target", lfo_target_roller_create);
}

// ============================================================================
// CC Slot Rollers
// ============================================================================

static void cc_slot_confirm_cb(uint32_t selected_index, void* user_data) {
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  uint8_t slot = (uint8_t)(uintptr_t)user_data;
  
  if (selected_index == 0) {
    scene->expression.cc_numbers[slot] = 0;
  } else if (selected_index < s_cc_options.count) {
    scene->expression.cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }
  
  // Recalculate num_cc_numbers
  scene->expression.num_cc_numbers = 0;
  for (int i = 0; i < 4; i++) {
    if (scene->expression.cc_numbers[i] > 0) {
      scene->expression.num_cc_numbers++;
    }
  }
  
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene || !s_cc_options.options_str) return NULL;
  
  uint8_t slot = s_editing_cc_slot;
  uint32_t current_idx = 0;
  
  if (slot < 4 && scene->expression.cc_numbers[slot] > 0) {
    current_idx = cc_number_to_option_index(scene->expression.cc_numbers[slot]);
  }
  
  static char title[32];
  snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(slot + 1));
  
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx,
    cc_slot_confirm_cb, (void*)(uintptr_t)slot);
}

static void nav_to_cc_slot(void* user_data) {
  s_editing_cc_slot = (uint8_t)(uintptr_t)user_data;
  if (!s_cc_options.options_str) load_cc_options();
  
  static char title[32];
  snprintf(title, sizeof(title), "CC Slot %u", (unsigned)(s_editing_cc_slot + 1));
  menu_navigate_to(title, cc_slot_roller_create);
}

// ============================================================================
// Signal Polarity Roller (Unipolar/Bipolar/Inverted)
// ============================================================================

static const char* polarity_to_string(polarity_t polarity) {
  switch (polarity) {
    case POLARITY_UNIPOLAR: return "Unipolar";
    case POLARITY_BIPOLAR: return "Bipolar";
    case POLARITY_INVERTED: return "Inverted";
    default: return "Unknown";
  }
}

static void polarity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.polarity = (polarity_t)selected_index;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Expression polarity set to: %s", polarity_to_string(scene->expression.polarity));
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* polarity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (uint32_t)scene->expression.polarity;
  if (current > 2) current = 0;
  
  return menu_create_roller_page("Polarity", "Unipolar\nBipolar\nInverted", current, polarity_confirm_cb, NULL);
}

static void nav_to_polarity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Polarity", polarity_roller_create);
}

// ============================================================================
// Curve Roller
// ============================================================================

static void curve_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.curve.type = (curve_type_t)selected_index;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Expression curve set to: %s", curve_type_to_string(scene->expression.curve.type));
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* curve_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  static char options[256];
  options[0] = '\0';
  
  for (int i = 0; i < CURVE_CUSTOM; i++) {
    if (i > 0) strcat(options, "\n");
    strcat(options, curve_type_to_string((curve_type_t)i));
  }
  
  uint32_t current = (uint32_t)scene->expression.curve.type;
  if (current >= CURVE_CUSTOM) current = 0;
  
  return menu_create_roller_page("Curve", options, current, curve_confirm_cb, NULL);
}

static void nav_to_curve(void* user_data) {
  (void)user_data;
  menu_navigate_to("Curve", curve_roller_create);
}

// ============================================================================
// Note Mode Configuration
// ============================================================================

static void base_note_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.base_note = (uint8_t)selected_index;
  persist_scene_changes();
  
  char note_name[8];
  get_note_name(scene->expression.base_note, note_name, sizeof(note_name));
  ESP_LOGI(TAG, "Expression base note set to: %s", note_name);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* base_note_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  static char options[1024];
  options[0] = '\0';
  
  for (int i = 0; i <= 127; i++) {
    char note_name[8];
    get_note_name((uint8_t)i, note_name, sizeof(note_name));
    if (i > 0) strcat(options, "\n");
    strcat(options, note_name);
  }
  
  uint32_t current = scene->expression.base_note;
  if (current > 127) current = 60;
  
  return menu_create_roller_page("Base Note", options, current, base_note_confirm_cb, NULL);
}

static void nav_to_base_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Base Note", base_note_roller_create);
}

static void range_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.note_range = (uint8_t)((selected_index + 1) * 12);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Expression note range set to: %u semitones", 
    (unsigned)scene->expression.note_range);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* range_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  static const char* options = 
    "1 Octave\n2 Octaves\n3 Octaves\n4 Octaves\n5 Octaves\n"
    "6 Octaves\n7 Octaves\n8 Octaves\n9 Octaves\n10 Octaves";
  
  uint8_t range = scene->expression.note_range;
  uint32_t current = (range / 12);
  if (current > 0) current--;
  if (current > 9) current = 0;
  
  return menu_create_roller_page("Range", options, current, range_confirm_cb, NULL);
}

static void nav_to_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Range", range_roller_create);
}

// ============================================================================
// Velocity Mode Roller (for Notes mode)
// ============================================================================

static void velocity_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  velocity_mode_t mode;
  switch (selected_index) {
    case 0: mode = VELOCITY_MODE_FIXED; break;
    case 1: mode = VELOCITY_MODE_GATE_VOLTAGE; break;
    case 2: mode = VELOCITY_MODE_TOUCHWHEEL; break;
    default: mode = VELOCITY_MODE_FIXED; break;
  }
  
  scene_set_expression_velocity_mode(scene_get_current_index(), mode);
  
  const char* mode_str = (mode == VELOCITY_MODE_FIXED) ? "Fixed" :
                         (mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Touchwheel";
  ESP_LOGI(TAG, "Expression velocity mode set to: %s", mode_str);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* velocity_mode_roller_create(void) {
  velocity_mode_t current = scene_get_expression_velocity_mode(scene_get_current_index());
  
  uint32_t current_idx;
  switch (current) {
    case VELOCITY_MODE_FIXED: current_idx = 0; break;
    case VELOCITY_MODE_GATE_VOLTAGE: current_idx = 1; break;
    case VELOCITY_MODE_TOUCHWHEEL: current_idx = 2; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Velocity Mode", "Fixed\nGate Voltage\nTouchwheel", current_idx,
    velocity_mode_confirm_cb, NULL);
}

static void nav_to_velocity_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity Mode", velocity_mode_roller_create);
}

// ============================================================================
// Velocity Roller (for Notes mode, only when velocity mode is Fixed)
// ============================================================================

static void velocity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->expression.velocity = (uint8_t)(selected_index + 1);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Expression velocity set to: %u", (unsigned)scene->expression.velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Expression", menu_page_expression_create);
}

static lv_obj_t* velocity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  static char options[640];
  options[0] = '\0';
  for (int i = 1; i <= 127; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, num);
  }
  
  uint8_t vel = scene->expression.velocity;
  if (vel == 0) vel = 100;
  uint32_t current = vel - 1;
  
  return menu_create_roller_page("Velocity", options, current, velocity_confirm_cb, NULL);
}

static void nav_to_velocity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity", velocity_roller_create);
}

// ============================================================================
// Switch Mode - Action Configuration
// ============================================================================

static void nav_to_switch_action(void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  s_switch_action_ctx.target_action = &scene->expr_switch;
  s_switch_action_ctx.source_title = "Expression";
  s_switch_action_ctx.detail_title = "Switch Action";
  s_switch_action_ctx.return_page = menu_page_expression_create;
  s_switch_action_ctx.return_depth = 2;
  s_switch_action_ctx.on_complete = NULL;
  s_switch_action_ctx.user_data = NULL;
  s_switch_action_ctx.trigger_type = ACTION_TRIGGER_EXPR_SWITCH;
  
  action_config_start(&s_switch_action_ctx);
}

// ============================================================================
// Main Expression Page
// ============================================================================

lv_obj_t* menu_page_expression_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("Expression", NULL, 0);
  }
  
  load_cc_options();
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  uint8_t scene_index = scene_get_current_index();
  expression_mode_t mode = scene_get_expression_mode(scene_index);
  
  // Check if CV is in CV/Gate mode (locks expression to Gate)
  input_mode_t cv_mode = scene_get_cv_input_mode(scene_index);
  bool locked_by_cv_gate = (cv_mode == INPUT_MODE_NOTE);

  // Check if either LFO is using expression for rate control
  bool locked_by_lfo = (scene->lfo1_config.rate_mode == LFO_RATE_MODE_EXPRESSION) ||
                       (scene->lfo2_config.rate_mode == LFO_RATE_MODE_EXPRESSION);

  // Mode selector (always first)
  if (locked_by_cv_gate) {
    // Show read-only mode indicating CV/Gate controls this
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\nGate (Locked)");
    s_expr_items[item_count++] = (menu_item_t){s_mode_label[buf], NULL, NULL, false};
  } else if (locked_by_lfo) {
    // Show read-only mode indicating LFO controls this
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\nLFO Rate (Locked)");
    s_expr_items[item_count++] = (menu_item_t){s_mode_label[buf], NULL, NULL, false};
  } else {
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\n%s", 
      get_mode_display_name(mode));
    s_expr_items[item_count++] = (menu_item_t){s_mode_label[buf], nav_to_mode, NULL, true};
  }
  
  // Mode-specific items
  switch (mode) {
    case EXPRESSION_MODE_PEDAL: {
      // Output type selector
      const char* output_name;
      switch (scene->expression.output_type) {
        case OUTPUT_TYPE_CC: output_name = "Control Change"; break;
        case OUTPUT_TYPE_NOTE: output_name = "Notes"; break;
        case OUTPUT_TYPE_LFO_RATE: output_name = "LFO Rate"; break;
        case OUTPUT_TYPE_LFO_DEPTH: output_name = "LFO Depth"; break;
        default: output_name = "Control Change"; break;
      }
      snprintf(s_output_label[buf], sizeof(s_output_label[buf]), "Output\n%s", output_name);
      s_expr_items[item_count++] = (menu_item_t){s_output_label[buf], nav_to_output, NULL, true};
      
      if (scene->expression.output_type == OUTPUT_TYPE_CC) {
        // CC slots
        uint8_t scene_idx = scene_get_current_index();
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_idx);
        
        for (int i = 0; i < 4; i++) {
          uint8_t cc_num = scene->expression.cc_numbers[i];
          if (cc_num > 0) {
            const char* cc_name = assets_get_cc_name(device, cc_num);
            if (cc_name && strcmp(cc_name, "Undefined") != 0) {
              snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
                "CC Slot %d\n%s", i + 1, cc_name);
            } else {
              snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
                "CC Slot %d\nCC %u", i + 1, (unsigned)cc_num);
            }
          } else {
            snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
              "CC Slot %d\nInactive", i + 1);
          }
          s_expr_items[item_count++] = (menu_item_t){
            s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true
          };
        }
        
        // Polarity (envelope shaping)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->expression.polarity));
        s_expr_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true};
        
        // Curve
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->expression.curve.type));
        s_expr_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true};
        
      } else if (scene->expression.output_type == OUTPUT_TYPE_NOTE) {
        // Notes mode: Base Note, Range, Velocity Mode, (Fixed) Velocity
        char note_name[8];
        get_note_name(scene->expression.base_note, note_name, sizeof(note_name));
        snprintf(s_base_note_label[buf], sizeof(s_base_note_label[buf]),
          "Base Note\n%s", note_name);
        s_expr_items[item_count++] = (menu_item_t){s_base_note_label[buf], nav_to_base_note, NULL, true};
        
        uint8_t octaves = scene->expression.note_range / 12;
        if (octaves == 0) octaves = 1;
        snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
          "Range\n%u Octave%s", (unsigned)octaves, octaves > 1 ? "s" : "");
        s_expr_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true};
        
        // Velocity mode
        velocity_mode_t vel_mode = scene_get_expression_velocity_mode(scene_get_current_index());
        const char* vel_mode_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" :
                                   (vel_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Touchwheel";
        snprintf(s_velocity_mode_label[buf], sizeof(s_velocity_mode_label[buf]),
          "Velocity Mode\n%s", vel_mode_str);
        s_expr_items[item_count++] = (menu_item_t){s_velocity_mode_label[buf], nav_to_velocity_mode, NULL, true};
        
        // Fixed velocity (only shown when mode is FIXED)
        if (vel_mode == VELOCITY_MODE_FIXED) {
          uint8_t vel = scene->expression.velocity;
          if (vel == 0) vel = 100;
          snprintf(s_velocity_label[buf], sizeof(s_velocity_label[buf]),
            "Velocity\n%u", (unsigned)vel);
          s_expr_items[item_count++] = (menu_item_t){s_velocity_label[buf], nav_to_velocity, NULL, true};
        }
        
        // Polarity (envelope shaping - also applies to notes)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->expression.polarity));
        s_expr_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true};
        
        // Curve (also applies to notes)
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->expression.curve.type));
        s_expr_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true};
      } else if (scene->expression.output_type == OUTPUT_TYPE_LFO_RATE ||
                 scene->expression.output_type == OUTPUT_TYPE_LFO_DEPTH) {
        // LFO modulation mode: Target selector
        snprintf(s_lfo_target_label[buf], sizeof(s_lfo_target_label[buf]),
          "LFO Target\n%s", lfo_target_to_string(scene->expression.lfo_target));
        s_expr_items[item_count++] = (menu_item_t){s_lfo_target_label[buf], nav_to_lfo_target, NULL, true};
      }
      
      break;
    }
    
    case EXPRESSION_MODE_NONE:
      // No additional menu items when disabled
      break;
    
    case EXPRESSION_MODE_SUSTAIN:
    case EXPRESSION_MODE_SOSTENUTO:
      // No additional menu items - these trigger fixed actions
      break;
    
    case EXPRESSION_MODE_SWITCH: {
      // Show action configuration
      const char* action_name = action_config_get_display_name(scene->expr_switch.type);
      snprintf(s_action_label[buf], sizeof(s_action_label[buf]),
        "Action\n%s", action_name);
      s_expr_items[item_count++] = (menu_item_t){s_action_label[buf], nav_to_switch_action, NULL, true};
      break;
    }
    
    case EXPRESSION_MODE_GATE:
      // Gate mode is externally controlled, show info only
      break;
    
    default:
      break;
  }
  
  return menu_create_page_2line("Expression", s_expr_items, item_count);
}

// Cleanup function
void menu_page_expression_cleanup(void) {
  free_cc_options();
  action_config_cleanup();
}
