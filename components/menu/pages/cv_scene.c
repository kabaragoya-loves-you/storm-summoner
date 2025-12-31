#include "menu.h"
#include "menu_pages.h"
#include "cv.h"
#include "scene.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "tempo.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_CV_SCENE"

// Forward declarations
lv_obj_t* menu_page_cv_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_CV_ITEMS 14
static menu_item_t s_cv_items[MAX_CV_ITEMS];

static char s_mode_label[LABEL_BUFFER_SETS][48];
static char s_output_label[LABEL_BUFFER_SETS][32];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_polarity_label[LABEL_BUFFER_SETS][32];
static char s_curve_label[LABEL_BUFFER_SETS][32];
static char s_base_note_label[LABEL_BUFFER_SETS][32];
static char s_range_label[LABEL_BUFFER_SETS][32];
static char s_velocity_label[LABEL_BUFFER_SETS][32];
static char s_vel_mode_label[LABEL_BUFFER_SETS][40];
static char s_fixed_vel_label[LABEL_BUFFER_SETS][32];
static char s_gate_info_label[LABEL_BUFFER_SETS][48];

// CC options from device
typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};
static uint8_t s_editing_cc_slot = 0;
static bool s_callback_in_progress = false;

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

static const char* get_cv_mode_display_name(input_mode_t mode) {
  switch (mode) {
    case INPUT_MODE_NONE: return "<None>";
    case INPUT_MODE_CV: return "CV";
    case INPUT_MODE_CLOCK_SYNC: return "Clock Sync";
    case INPUT_MODE_AUDIO: return "Audio";
    case INPUT_MODE_NOTE: return "Note Mode";
    default: return "Unknown";
  }
}

static const char* polarity_to_string(polarity_t polarity) {
  switch (polarity) {
    case POLARITY_UNIPOLAR: return "Unipolar";
    case POLARITY_BIPOLAR: return "Bipolar";
    case POLARITY_INVERTED: return "Inverted";
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
  
  // Map index to mode
  // 0=None, 1=CV, 2=Note Mode, 3=Audio (not selectable)
  input_mode_t modes[] = {
    INPUT_MODE_NONE,
    INPUT_MODE_CV,
    INPUT_MODE_NOTE,
    INPUT_MODE_AUDIO  // shown grayed out, shouldn't be selected
  };
  
  if (selected_index >= 3) {
    // Audio mode is "coming soon" - don't allow selection
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  input_mode_t new_mode = modes[selected_index];
  uint8_t scene_index = scene_get_current_index();
  
  esp_err_t ret = scene_set_cv_input_mode(scene_index, new_mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "CV input mode set to: %s", get_cv_mode_display_name(new_mode));
    
    // Apply the mode to hardware if this is the current scene
    if (new_mode != INPUT_MODE_NONE) {
      input_set_mode(new_mode);
    }
  } else {
    ESP_LOGW(TAG, "Failed to set CV input mode: %s", esp_err_to_name(ret));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* mode_roller_create(void) {
  // Options: None, CV, Note Mode, Audio (coming soon)
  static const char* options = "<None>\nCV\nNote Mode\nAudio (coming soon)";
  
  uint8_t scene_index = scene_get_current_index();
  input_mode_t current = scene_get_cv_input_mode(scene_index);
  
  uint32_t current_idx = 0;
  switch (current) {
    case INPUT_MODE_NONE: current_idx = 0; break;
    case INPUT_MODE_CV: current_idx = 1; break;
    case INPUT_MODE_NOTE: current_idx = 2; break;
    case INPUT_MODE_AUDIO: current_idx = 3; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Mode", options, current_idx, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Output Type Roller (CC vs Notes) - For CV Mode
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
  
  scene->cv.output_type = (selected_index == 0) ? OUTPUT_TYPE_CC : OUTPUT_TYPE_NOTE;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "CV output set to: %s", 
    scene->cv.output_type == OUTPUT_TYPE_CC ? "CC" : "Notes");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* output_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (scene->cv.output_type == OUTPUT_TYPE_CC) ? 0 : 1;
  return menu_create_roller_page("Output", "Control Change\nNotes", current, output_confirm_cb, NULL);
}

static void nav_to_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Output", output_roller_create);
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
    scene->cv.cc_numbers[slot] = 0;
  } else if (selected_index < s_cc_options.count) {
    scene->cv.cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }
  
  // Recalculate num_cc_numbers
  scene->cv.num_cc_numbers = 0;
  for (int i = 0; i < 4; i++) {
    if (scene->cv.cc_numbers[i] > 0) {
      scene->cv.num_cc_numbers++;
    }
  }
  
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    load_cc_options();
  }
  
  if (!s_cc_options.options_str) return NULL;
  
  uint8_t current_cc = scene->cv.cc_numbers[s_editing_cc_slot];
  uint32_t current_idx = (current_cc == 0) ? 0 : cc_number_to_option_index(current_cc);
  
  char title[16];
  snprintf(title, sizeof(title), "CC Slot %d", s_editing_cc_slot + 1);
  
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx, 
    cc_slot_confirm_cb, (void*)(uintptr_t)s_editing_cc_slot);
}

static void nav_to_cc_slot(void* user_data) {
  s_editing_cc_slot = (uint8_t)(uintptr_t)user_data;
  char title[16];
  snprintf(title, sizeof(title), "CC Slot %d", s_editing_cc_slot + 1);
  menu_navigate_to(title, cc_slot_roller_create);
}

// ============================================================================
// Polarity Roller (Envelope Shaping)
// ============================================================================

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
  
  polarity_t polarities[] = { POLARITY_UNIPOLAR, POLARITY_BIPOLAR, POLARITY_INVERTED };
  if (selected_index < 3) {
    scene->cv.polarity = polarities[selected_index];
    persist_scene_changes();
    ESP_LOGI(TAG, "CV polarity set to: %s", polarity_to_string(scene->cv.polarity));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* polarity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->cv.polarity) {
    case POLARITY_UNIPOLAR: current = 0; break;
    case POLARITY_BIPOLAR: current = 1; break;
    case POLARITY_INVERTED: current = 2; break;
  }
  
  return menu_create_roller_page("Polarity", "Unipolar\nBipolar\nInverted", current, 
    polarity_confirm_cb, NULL);
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
  
  curve_type_t curves[] = { CURVE_LINEAR, CURVE_EXPONENTIAL, CURVE_LOGARITHMIC, CURVE_S_CURVE };
  if (selected_index < 4) {
    scene->cv.curve.type = curves[selected_index];
    persist_scene_changes();
    ESP_LOGI(TAG, "CV curve set to: %s", curve_type_to_string(scene->cv.curve.type));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* curve_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->cv.curve.type) {
    case CURVE_LINEAR: current = 0; break;
    case CURVE_EXPONENTIAL: current = 1; break;
    case CURVE_LOGARITHMIC: current = 2; break;
    case CURVE_S_CURVE: current = 3; break;
    default: current = 0; break;
  }
  
  return menu_create_roller_page("Curve", "Linear\nExponential\nLogarithmic\nS-Curve", current, 
    curve_confirm_cb, NULL);
}

static void nav_to_curve(void* user_data) {
  (void)user_data;
  menu_navigate_to("Curve", curve_roller_create);
}

// ============================================================================
// Base Note Roller (for CV Note Output)
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
  
  scene->cv.base_note = (uint8_t)selected_index;
  persist_scene_changes();
  
  char note_name[8];
  get_note_name(scene->cv.base_note, note_name, sizeof(note_name));
  ESP_LOGI(TAG, "CV base note set to: %s (%d)", note_name, scene->cv.base_note);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* base_note_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Build note options (128 notes)
  static char options[1536];
  options[0] = '\0';
  
  for (int i = 0; i < 128; i++) {
    char note[8];
    get_note_name(i, note, sizeof(note));
    if (i > 0) strcat(options, "\n");
    strcat(options, note);
  }
  
  uint32_t current = scene->cv.base_note;
  return menu_create_roller_page("Base Note", options, current, base_note_confirm_cb, NULL);
}

static void nav_to_base_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Base Note", base_note_roller_create);
}

// ============================================================================
// Range Roller (for CV Note Output)
// ============================================================================

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
  
  // Range in octaves (1-8)
  uint8_t octaves = (uint8_t)(selected_index + 1);
  scene->cv.note_range = octaves * 12;
  persist_scene_changes();
  
  ESP_LOGI(TAG, "CV note range set to: %d octave%s (%d semitones)", 
    octaves, octaves > 1 ? "s" : "", scene->cv.note_range);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* range_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  static const char* options = "1 Octave\n2 Octaves\n3 Octaves\n4 Octaves\n5 Octaves\n6 Octaves\n7 Octaves\n8 Octaves";
  
  uint8_t octaves = scene->cv.note_range / 12;
  if (octaves == 0) octaves = 1;
  if (octaves > 8) octaves = 8;
  uint32_t current = octaves - 1;
  
  return menu_create_roller_page("Range", options, current, range_confirm_cb, NULL);
}

static void nav_to_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Range", range_roller_create);
}

// ============================================================================
// Velocity Roller (for CV Note Output)
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
  
  scene->cv.velocity = (uint8_t)(selected_index + 1);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "CV velocity set to: %d", scene->cv.velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* velocity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Build velocity options (1-127)
  static char options[512];
  options[0] = '\0';
  
  for (int i = 1; i <= 127; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, num);
  }
  
  uint8_t vel = scene->cv.velocity;
  if (vel == 0) vel = 100;
  uint32_t current = vel - 1;
  
  return menu_create_roller_page("Velocity", options, current, velocity_confirm_cb, NULL);
}

static void nav_to_velocity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity", velocity_roller_create);
}

// ============================================================================
// Note Mode Velocity Settings (for INPUT_MODE_NOTE)
// ============================================================================

static void note_vel_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t scene_index = scene_get_current_index();
  velocity_mode_t mode = (selected_index == 0) ? VELOCITY_MODE_FIXED : VELOCITY_MODE_GATE_VOLTAGE;
  
  scene_set_note_velocity_mode(scene_index, mode);
  
  ESP_LOGI(TAG, "Note velocity mode set to: %s", 
    mode == VELOCITY_MODE_FIXED ? "Fixed" : "Gate Voltage");
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* note_vel_mode_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  velocity_mode_t current = scene_get_note_velocity_mode(scene_index);
  
  uint32_t current_idx = (current == VELOCITY_MODE_FIXED) ? 0 : 1;
  return menu_create_roller_page("Velocity Mode", "Fixed\nGate Voltage", current_idx, 
    note_vel_mode_confirm_cb, NULL);
}

static void nav_to_note_vel_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity Mode", note_vel_mode_roller_create);
}

// ============================================================================
// Note Mode Fixed Velocity (for INPUT_MODE_NOTE)
// ============================================================================

static void note_fixed_vel_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t scene_index = scene_get_current_index();
  uint8_t velocity = (uint8_t)(selected_index + 1);
  
  scene_set_note_fixed_velocity(scene_index, velocity);
  
  ESP_LOGI(TAG, "Note fixed velocity set to: %d", velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "CV", menu_page_cv_scene_create);
}

static lv_obj_t* note_fixed_vel_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  uint8_t current_vel = scene_get_note_fixed_velocity(scene_index);
  if (current_vel == 0) current_vel = 100;
  
  // Build velocity options (1-127)
  static char options[512];
  options[0] = '\0';
  
  for (int i = 1; i <= 127; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, num);
  }
  
  uint32_t current = current_vel - 1;
  return menu_create_roller_page("Fixed Velocity", options, current, note_fixed_vel_confirm_cb, NULL);
}

static void nav_to_note_fixed_vel(void* user_data) {
  (void)user_data;
  menu_navigate_to("Fixed Velocity", note_fixed_vel_roller_create);
}

// ============================================================================
// Main CV Scene Page
// ============================================================================

lv_obj_t* menu_page_cv_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("CV", NULL, 0);
  }
  
  load_cc_options();
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  uint8_t scene_index = scene_get_current_index();
  input_mode_t mode = scene_get_cv_input_mode(scene_index);
  
  // Check if tempo is using clock sync (overrides CV input mode)
  tempo_clock_source_t clock_source = scene->clock_source;
  bool clock_sync_active = (clock_source == CLOCK_SOURCE_SYNC);
  
  if (clock_sync_active) {
    // Show read-only mode indicating tempo controls this
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), 
      "Mode\nClock Sync (Tempo)");
    s_cv_items[item_count++] = (menu_item_t){s_mode_label[buf], NULL, NULL, false};
    
    // Show info that CV is controlled by tempo
    return menu_create_page_2line("CV", s_cv_items, item_count);
  }
  
  // Mode selector (always first)
  snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\n%s", 
    get_cv_mode_display_name(mode));
  s_cv_items[item_count++] = (menu_item_t){s_mode_label[buf], nav_to_mode, NULL, true};
  
  // Mode-specific items
  switch (mode) {
    case INPUT_MODE_CV: {
      // Output type selector
      const char* output_name = (scene->cv.output_type == OUTPUT_TYPE_CC) ? 
        "Control Change" : "Notes";
      snprintf(s_output_label[buf], sizeof(s_output_label[buf]), "Output\n%s", output_name);
      s_cv_items[item_count++] = (menu_item_t){s_output_label[buf], nav_to_output, NULL, true};
      
      if (scene->cv.output_type == OUTPUT_TYPE_CC) {
        // CC slots
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
        
        for (int i = 0; i < 4; i++) {
          uint8_t cc_num = scene->cv.cc_numbers[i];
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
          s_cv_items[item_count++] = (menu_item_t){
            s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true
          };
        }
        
        // Polarity (envelope shaping)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->cv.polarity));
        s_cv_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true};
        
        // Curve
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->cv.curve.type));
        s_cv_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true};
        
      } else {
        // Notes output mode: Base Note, Range, Velocity
        char note_name[8];
        get_note_name(scene->cv.base_note, note_name, sizeof(note_name));
        snprintf(s_base_note_label[buf], sizeof(s_base_note_label[buf]),
          "Base Note\n%s", note_name);
        s_cv_items[item_count++] = (menu_item_t){s_base_note_label[buf], nav_to_base_note, NULL, true};
        
        uint8_t octaves = scene->cv.note_range / 12;
        if (octaves == 0) octaves = 1;
        snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
          "Range\n%u Octave%s", (unsigned)octaves, octaves > 1 ? "s" : "");
        s_cv_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true};
        
        uint8_t vel = scene->cv.velocity;
        if (vel == 0) vel = 100;
        snprintf(s_velocity_label[buf], sizeof(s_velocity_label[buf]),
          "Velocity\n%u", (unsigned)vel);
        s_cv_items[item_count++] = (menu_item_t){s_velocity_label[buf], nav_to_velocity, NULL, true};
        
        // Polarity (envelope shaping - also applies to notes)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->cv.polarity));
        s_cv_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true};
        
        // Curve (also applies to notes)
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->cv.curve.type));
        s_cv_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true};
      }
      
      break;
    }
    
    case INPUT_MODE_NOTE: {
      // Note Mode: CV pitch + Expression gate
      // Show velocity mode
      velocity_mode_t vel_mode = scene_get_note_velocity_mode(scene_index);
      snprintf(s_vel_mode_label[buf], sizeof(s_vel_mode_label[buf]),
        "Velocity Mode\n%s", vel_mode == VELOCITY_MODE_FIXED ? "Fixed" : "Gate Voltage");
      s_cv_items[item_count++] = (menu_item_t){s_vel_mode_label[buf], nav_to_note_vel_mode, NULL, true};
      
      // Show fixed velocity value if in fixed mode
      if (vel_mode == VELOCITY_MODE_FIXED) {
        uint8_t fixed_vel = scene_get_note_fixed_velocity(scene_index);
        if (fixed_vel == 0) fixed_vel = 100;
        snprintf(s_fixed_vel_label[buf], sizeof(s_fixed_vel_label[buf]),
          "Fixed Velocity\n%u", (unsigned)fixed_vel);
        s_cv_items[item_count++] = (menu_item_t){s_fixed_vel_label[buf], nav_to_note_fixed_vel, NULL, true};
      }
      
      // Show info that Expression is locked to Gate mode
      snprintf(s_gate_info_label[buf], sizeof(s_gate_info_label[buf]),
        "Expression\nLocked to Gate");
      s_cv_items[item_count++] = (menu_item_t){s_gate_info_label[buf], NULL, NULL, false};
      
      break;
    }
    
    case INPUT_MODE_NONE:
      // No additional menu items when disabled
      break;
    
    case INPUT_MODE_AUDIO:
      // Audio mode is future - show coming soon
      break;
    
    case INPUT_MODE_CLOCK_SYNC:
      // This case is handled above (clock_sync_active)
      break;
    
    default:
      break;
  }
  
  return menu_create_page_2line("CV", s_cv_items, item_count);
}

// Cleanup function
void menu_page_cv_scene_cleanup(void) {
  free_cc_options();
}

