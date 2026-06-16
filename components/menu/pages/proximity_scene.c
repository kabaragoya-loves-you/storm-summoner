#include "menu.h"
#include "menu_pages.h"
#include "sensor.h"
#include "scene.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "proximity_mode_mapping.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_PROXIMITY_SCENE"

// Forward declarations
lv_obj_t* menu_page_proximity_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_PROXIMITY_ITEMS 12
static menu_item_t s_prox_items[MAX_PROXIMITY_ITEMS];

static char s_mode_label[LABEL_BUFFER_SETS][40];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_polarity_label[LABEL_BUFFER_SETS][32];
static char s_curve_label[LABEL_BUFFER_SETS][32];
static char s_base_note_label[LABEL_BUFFER_SETS][32];
static char s_range_label[LABEL_BUFFER_SETS][32];
static char s_velocity_mode_label[LABEL_BUFFER_SETS][32];
static char s_velocity_label[LABEL_BUFFER_SETS][32];
static char s_lfo_target_label[LABEL_BUFFER_SETS][32];
static char s_nudge_label[LABEL_BUFFER_SETS][32];
static char s_direction_label[LABEL_BUFFER_SETS][32];

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

  const proximity_mode_mapping_t* mapping = proximity_get_mode_mapping((uint8_t)selected_index);
  if (!mapping) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->proximity.enabled = mapping->enabled;
  scene->proximity.output_type = mapping->output_type;
  persist_scene_changes();

  if (scene->proximity.enabled) ps_enable();
  else ps_disable();

  ESP_LOGI(TAG, "Proximity mode set to: %s", mapping->display_name);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* mode_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (uint8_t i = 0; i < NUM_PROXIMITY_USER_MODES; i++) {
    if (i > 0) strcat(options, "\n");
    strcat(options, proximity_get_mode_name(i));
  }

  uint32_t current_idx = proximity_get_current_mode_index(scene_get_current());
  return menu_create_roller_page("Mode", options, current_idx, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
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
  
  scene->proximity.lfo_target = (lfo_target_t)selected_index;
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (uint32_t)scene->proximity.lfo_target;
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
    scene->proximity.cc_numbers[slot] = 0;
  } else if (selected_index < s_cc_options.count) {
    scene->proximity.cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }
  
  // Recalculate num_cc_numbers
  scene->proximity.num_cc_numbers = 0;
  for (int i = 0; i < 4; i++) {
    if (scene->proximity.cc_numbers[i] > 0) {
      scene->proximity.num_cc_numbers++;
    }
  }
  
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    load_cc_options();
  }
  
  if (!s_cc_options.options_str) return NULL;
  
  uint8_t current_cc = scene->proximity.cc_numbers[s_editing_cc_slot];
  uint32_t current_idx = (current_cc == 0) ? 0 : cc_number_to_option_index(current_cc);
  
  char title[24];
  snprintf(title, sizeof(title), "Control Change %d", s_editing_cc_slot + 1);

  return menu_create_roller_page(title, s_cc_options.options_str, current_idx,
    cc_slot_confirm_cb, (void*)(uintptr_t)s_editing_cc_slot);
}

static void nav_to_cc_slot(void* user_data) {
  s_editing_cc_slot = (uint8_t)(uintptr_t)user_data;
  char title[24];
  snprintf(title, sizeof(title), "Control Change %d", s_editing_cc_slot + 1);
  menu_navigate_to(title, cc_slot_roller_create);
}

// ============================================================================
// Polarity Roller
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
    scene->proximity.polarity = polarities[selected_index];
    persist_scene_changes();
    ESP_LOGI(TAG, "Proximity polarity set to: %s", polarity_to_string(scene->proximity.polarity));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* polarity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->proximity.polarity) {
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
    scene->proximity.curve.type = curves[selected_index];
    persist_scene_changes();
    ESP_LOGI(TAG, "Proximity curve set to: %s", curve_type_to_string(scene->proximity.curve.type));
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* curve_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->proximity.curve.type) {
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
// Base Note Roller (for Theremin/Notes mode)
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
  
  scene->proximity.base_note = (uint8_t)selected_index;
  persist_scene_changes();
  
  char note_name[8];
  get_note_name(scene->proximity.base_note, note_name, sizeof(note_name));
  ESP_LOGI(TAG, "Proximity base note set to: %s (%d)", note_name, scene->proximity.base_note);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
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
  
  uint32_t current = scene->proximity.base_note;
  return menu_create_roller_page("Base Note", options, current, base_note_confirm_cb, NULL);
}

static void nav_to_base_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Base Note", base_note_roller_create);
}

// ============================================================================
// Range Roller (for Notes mode - octaves)
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
  
  // Convert octave index to semitones (1 octave = 12 semitones)
  scene->proximity.note_range = (uint8_t)((selected_index + 1) * 12);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Proximity note range set to: %d octaves (%d semitones)", 
    selected_index + 1, scene->proximity.note_range);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* range_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Options: 1-5 octaves
  static const char* options = "1 Octave\n2 Octaves\n3 Octaves\n4 Octaves\n5 Octaves";
  
  uint32_t current = (scene->proximity.note_range / 12);
  if (current > 0) current--;
  if (current > 4) current = 4;
  
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
    case 1: mode = VELOCITY_MODE_GATE_VOLTAGE; break;
    case 2: mode = VELOCITY_MODE_TOUCHWHEEL; break;
    default: mode = VELOCITY_MODE_FIXED; break;
  }
  
  scene_set_proximity_velocity_mode(scene_get_current_index(), mode);
  
  const char* mode_str = (mode == VELOCITY_MODE_FIXED) ? "Fixed" :
    (mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Touchwheel";
  ESP_LOGI(TAG, "Proximity velocity mode set to: %s", mode_str);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* velocity_mode_roller_create(void) {
  velocity_mode_t current = scene_get_proximity_velocity_mode(scene_get_current_index());
  uint32_t current_idx;
  switch (current) {
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
  
  scene->proximity.velocity = (uint8_t)(selected_index + 1);
  persist_scene_changes();
  
  ESP_LOGI(TAG, "Proximity velocity set to: %d", scene->proximity.velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
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
  
  uint8_t vel = scene->proximity.velocity;
  if (vel == 0) vel = 100;
  uint32_t current = vel - 1;
  
  return menu_create_roller_page("Velocity", options, current, velocity_confirm_cb, NULL);
}

static void nav_to_velocity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Velocity", velocity_roller_create);
}

// ============================================================================
// Tempo Nudge % Roller (0..100, step 5)
// ============================================================================

static void nudge_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  uint8_t pct = (uint8_t)(selected_index * 5);
  if (pct > 100) pct = 100;
  scene_set_proximity_tempo_nudge_pct(scene_get_current_index(), pct);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* nudge_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 0; v <= 100; v += 5) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%%s", v, v < 100 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }

  uint8_t cur = scene_get_proximity_tempo_nudge_pct(scene_get_current_index());
  if (cur > 100) cur = 100;
  uint32_t idx = cur / 5;
  return menu_create_roller_page("Nudge %", options, idx, nudge_confirm_cb, NULL);
}

static void nav_to_nudge(void* user_data) {
  (void)user_data;
  menu_navigate_to("Nudge %", nudge_roller_create);
}

// ============================================================================
// Tempo Nudge Direction Roller
// ============================================================================

static const char* tempo_nudge_direction_to_string(uint8_t dir) {
  switch (dir) {
    case TEMPO_NUDGE_DIR_FASTER: return "Faster";
    case TEMPO_NUDGE_DIR_SLOWER: return "Slower";
    default: return "Both";
  }
}

static void direction_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  if (selected_index > TEMPO_NUDGE_DIR_SLOWER) selected_index = TEMPO_NUDGE_DIR_BOTH;
  scene_set_proximity_tempo_nudge_direction(scene_get_current_index(), (uint8_t)selected_index);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Proximity", menu_page_proximity_scene_create);
}

static lv_obj_t* direction_roller_create(void) {
  uint8_t cur = scene_get_proximity_tempo_nudge_direction(scene_get_current_index());
  if (cur > TEMPO_NUDGE_DIR_SLOWER) cur = TEMPO_NUDGE_DIR_BOTH;
  return menu_create_roller_page("Direction", "Both\nFaster\nSlower",
    (uint32_t)cur, direction_confirm_cb, NULL);
}

static void nav_to_direction(void* user_data) {
  (void)user_data;
  menu_navigate_to("Direction", direction_roller_create);
}

// ============================================================================
// Main Proximity Scene Page
// ============================================================================

lv_obj_t* menu_page_proximity_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("Proximity", NULL, 0);
  }
  
  load_cc_options();
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

  if (scene_cv_claims_source(VELOCITY_MODE_PROXIMITY)) {
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "CV/Gate\nControlled");
    s_prox_items[item_count++] = (menu_item_t){
      s_mode_label[buf], NULL, NULL, false, MENU_ITEM_KIND_DISPLAY
    };
    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(scene->proximity.polarity));
    s_prox_items[item_count++] = (menu_item_t){
      s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER
    };
    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(scene->proximity.curve.type));
    s_prox_items[item_count++] = (menu_item_t){
      s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER
    };
    return menu_create_page_2line("Proximity", s_prox_items, item_count);
  }
  
  uint8_t mode_idx = proximity_get_current_mode_index(scene);
  snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\n%s",
    proximity_get_mode_name(mode_idx));
  s_prox_items[item_count++] = (menu_item_t){
    s_mode_label[buf], nav_to_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (!scene->proximity.enabled) {
    return menu_create_page_2line("Proximity", s_prox_items, item_count);
  }

  if (scene->proximity.output_type == OUTPUT_TYPE_CC) {
    // CC slots
    for (int i = 0; i < 4; i++) {
      uint8_t cc_num = scene->proximity.cc_numbers[i];
      if (cc_num > 0) {
        const char* cc_name = assets_get_cc_name(device, cc_num);
        if (cc_name && strcmp(cc_name, "Undefined") != 0) {
          snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
            "Control Change %d\n%s", i + 1, cc_name);
        } else {
          snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
            "Control Change %d\nCC %u", i + 1, (unsigned)cc_num);
        }
      } else {
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
          "Control Change %d\nInactive", i + 1);
      }
      s_prox_items[item_count++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true,
        MENU_ITEM_KIND_ROLLER
      };
    }
    
    // Polarity
    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(scene->proximity.polarity));
    s_prox_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    // Curve
    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(scene->proximity.curve.type));
    s_prox_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};
    
  } else if (scene->proximity.output_type == OUTPUT_TYPE_NOTE) {
    // Notes (Theremin) output mode: Base Note, Range, Velocity Mode, (Fixed) Velocity
    char note_name[8];
    get_note_name(scene->proximity.base_note, note_name, sizeof(note_name));
    snprintf(s_base_note_label[buf], sizeof(s_base_note_label[buf]),
      "Base Note\n%s", note_name);
    s_prox_items[item_count++] = (menu_item_t){s_base_note_label[buf], nav_to_base_note, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    uint8_t octaves = scene->proximity.note_range / 12;
    if (octaves == 0) octaves = 1;
    snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
      "Range\n%u Octave%s", (unsigned)octaves, octaves > 1 ? "s" : "");
    s_prox_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    // Velocity mode
    velocity_mode_t vel_mode = scene_get_proximity_velocity_mode(scene_get_current_index());
    const char* vel_mode_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" :
      (vel_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Touchwheel";
    snprintf(s_velocity_mode_label[buf], sizeof(s_velocity_mode_label[buf]),
      "Velocity Mode\n%s", vel_mode_str);
    s_prox_items[item_count++] = (menu_item_t){s_velocity_mode_label[buf], nav_to_velocity_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    // Fixed velocity (only shown when mode is FIXED)
    if (vel_mode == VELOCITY_MODE_FIXED) {
      uint8_t vel = scene->proximity.velocity;
      if (vel == 0) vel = 100;
      snprintf(s_velocity_label[buf], sizeof(s_velocity_label[buf]),
        "Velocity\n%u", (unsigned)vel);
      s_prox_items[item_count++] = (menu_item_t){s_velocity_label[buf], nav_to_velocity, NULL, true, MENU_ITEM_KIND_ROLLER};
    }
    
    // Polarity (also applies to notes)
    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(scene->proximity.polarity));
    s_prox_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
    
    // Curve (also applies to notes)
    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(scene->proximity.curve.type));
    s_prox_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};
  } else if (scene->proximity.output_type == OUTPUT_TYPE_LFO_RATE ||
             scene->proximity.output_type == OUTPUT_TYPE_LFO_DEPTH) {
    // LFO modulation mode: Target selector
    snprintf(s_lfo_target_label[buf], sizeof(s_lfo_target_label[buf]),
      "LFO Target\n%s", lfo_target_to_string(scene->proximity.lfo_target));
    s_prox_items[item_count++] = (menu_item_t){s_lfo_target_label[buf], nav_to_lfo_target, NULL, true, MENU_ITEM_KIND_ROLLER};
  } else if (scene->proximity.output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    uint8_t pct = scene_get_proximity_tempo_nudge_pct(scene_get_current_index());
    snprintf(s_nudge_label[buf], sizeof(s_nudge_label[buf]),
      "Nudge %%\n%u%%", (unsigned)pct);
    s_prox_items[item_count++] = (menu_item_t){s_nudge_label[buf], nav_to_nudge, NULL, true, MENU_ITEM_KIND_ROLLER};

    uint8_t dir = scene_get_proximity_tempo_nudge_direction(scene_get_current_index());
    snprintf(s_direction_label[buf], sizeof(s_direction_label[buf]),
      "Direction\n%s", tempo_nudge_direction_to_string(dir));
    s_prox_items[item_count++] = (menu_item_t){
      s_direction_label[buf], nav_to_direction, NULL, true, MENU_ITEM_KIND_ROLLER
    };
  }
  
  return menu_create_page_2line("Proximity", s_prox_items, item_count);
}

// Cleanup function
void menu_page_proximity_scene_cleanup(void) {
  free_cc_options();
}

