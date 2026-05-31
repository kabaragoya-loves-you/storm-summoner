#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "tilt.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_TILT_AXIS_SCENE"

// ============================================================================
// Axis state (shared with tilt_scene.c via tiny setter)
// ============================================================================

static tilt_axis_t s_axis = TILT_AXIS_X;

void menu_tilt_scene_set_axis(tilt_axis_t axis) { s_axis = axis; }

static const char* axis_title(void) { return (s_axis == TILT_AXIS_X) ? "Tilt X" : "Tilt Y"; }

static continuous_mapping_t* get_mapping(scene_t* scene) {
  if (!scene) return NULL;
  return (s_axis == TILT_AXIS_X) ? &scene->tilt_x : &scene->tilt_y;
}

static velocity_mode_t get_vel_mode(uint8_t scene_index) {
  return (s_axis == TILT_AXIS_X)
    ? scene_get_tilt_x_velocity_mode(scene_index)
    : scene_get_tilt_y_velocity_mode(scene_index);
}

static esp_err_t set_vel_mode(uint8_t scene_index, velocity_mode_t mode) {
  return (s_axis == TILT_AXIS_X)
    ? scene_set_tilt_x_velocity_mode(scene_index, mode)
    : scene_set_tilt_y_velocity_mode(scene_index, mode);
}

static uint8_t get_nudge_pct(uint8_t scene_index) {
  return (s_axis == TILT_AXIS_X)
    ? scene_get_tilt_x_tempo_nudge_pct(scene_index)
    : scene_get_tilt_y_tempo_nudge_pct(scene_index);
}

static esp_err_t set_nudge_pct(uint8_t scene_index, uint8_t pct) {
  return (s_axis == TILT_AXIS_X)
    ? scene_set_tilt_x_tempo_nudge_pct(scene_index, pct)
    : scene_set_tilt_y_tempo_nudge_pct(scene_index, pct);
}

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_ITEMS 14
static menu_item_t s_items[MAX_ITEMS];

// Tracks the live axis screen so our custom back handler can tell whether
// the user is backing out of the axis page itself or out of a deeper page
// (roller). If they're deeper, we fall through to default navigation.
static lv_obj_t* s_axis_screen = NULL;

static char s_enabled_label[LABEL_BUFFER_SETS][32];
static char s_output_label[LABEL_BUFFER_SETS][32];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_polarity_label[LABEL_BUFFER_SETS][32];
static char s_curve_label[LABEL_BUFFER_SETS][32];
static char s_base_note_label[LABEL_BUFFER_SETS][32];
static char s_range_label[LABEL_BUFFER_SETS][32];
static char s_velocity_mode_label[LABEL_BUFFER_SETS][32];
static char s_velocity_label[LABEL_BUFFER_SETS][32];
static char s_lfo_target_label[LABEL_BUFFER_SETS][32];
static char s_nudge_label[LABEL_BUFFER_SETS][32];
static char s_min_label[LABEL_BUFFER_SETS][32];
static char s_middle_label[LABEL_BUFFER_SETS][32];
static char s_max_label[LABEL_BUFFER_SETS][32];

typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};
static uint8_t s_editing_cc_slot = 0;
static bool s_callback_in_progress = false;

static const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// ============================================================================
// Helpers
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

static const char* lfo_target_to_string(lfo_target_t target) {
  switch (target) {
    case LFO_TARGET_LFO1: return "LFO1";
    case LFO_TARGET_LFO2: return "LFO2";
    case LFO_TARGET_BOTH: return "Both";
    default: return "Both";
  }
}

// ============================================================================
// CC Options Loading (mirrors als_scene.c)
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
// Enabled Roller
// ============================================================================

static void enabled_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->enabled = (selected_index == 1);
  persist_scene_changes();

  // Drive the sampling task state to avoid useless I2C traffic.
  tilt_axis_set_enabled(s_axis, m->enabled);

  ESP_LOGI(TAG, "Tilt %c %s", s_axis == TILT_AXIS_X ? 'X' : 'Y',
    m->enabled ? "enabled" : "disabled");

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* enabled_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  uint32_t current = (m && m->enabled) ? 1 : 0;
  return menu_create_roller_page(axis_title(), "Disabled\nEnabled", current, enabled_confirm_cb, NULL);
}

static void nav_to_enabled(void* user_data) {
  (void)user_data;
  menu_navigate_to(axis_title(), enabled_roller_create);
}

// ============================================================================
// Output Type Roller
// ============================================================================

// Option indices: 0=CC, 1=Notes, 2=LFO Rate, 3=LFO Depth, 4=Pitch Bend, 5=Tempo Nudge
static output_type_t output_index_to_type(uint32_t idx) {
  switch (idx) {
    case 0: return OUTPUT_TYPE_CC;
    case 1: return OUTPUT_TYPE_NOTE;
    case 2: return OUTPUT_TYPE_LFO_RATE;
    case 3: return OUTPUT_TYPE_LFO_DEPTH;
    case 4: return OUTPUT_TYPE_PITCH_BEND;
    case 5: return OUTPUT_TYPE_TEMPO_NUDGE;
    default: return OUTPUT_TYPE_CC;
  }
}

static uint32_t output_type_to_index(output_type_t t) {
  switch (t) {
    case OUTPUT_TYPE_CC: return 0;
    case OUTPUT_TYPE_NOTE: return 1;
    case OUTPUT_TYPE_LFO_RATE: return 2;
    case OUTPUT_TYPE_LFO_DEPTH: return 3;
    case OUTPUT_TYPE_PITCH_BEND: return 4;
    case OUTPUT_TYPE_TEMPO_NUDGE: return 5;
    default: return 0;
  }
}

static const char* output_type_name(output_type_t t) {
  switch (t) {
    case OUTPUT_TYPE_CC: return "Control Change";
    case OUTPUT_TYPE_NOTE: return "Notes";
    case OUTPUT_TYPE_LFO_RATE: return "LFO Rate";
    case OUTPUT_TYPE_LFO_DEPTH: return "LFO Depth";
    case OUTPUT_TYPE_PITCH_BEND: return "Pitch Bend";
    case OUTPUT_TYPE_TEMPO_NUDGE: return "Tempo Nudge";
    default: return "Control Change";
  }
}

static void output_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->output_type = output_index_to_type(selected_index);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* output_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  uint32_t current = output_type_to_index(m->output_type);
  return menu_create_roller_page("Output",
    "Control Change\nNotes\nLFO Rate\nLFO Depth\nPitch Bend\nTempo Nudge",
    current, output_confirm_cb, NULL);
}

static void nav_to_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Output", output_roller_create);
}

// ============================================================================
// LFO Target Roller
// ============================================================================

static void lfo_target_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->lfo_target = (lfo_target_t)selected_index;
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;
  uint32_t current = (uint32_t)m->lfo_target;
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
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  uint8_t slot = (uint8_t)(uintptr_t)user_data;

  if (selected_index == 0) {
    m->cc_numbers[slot] = 0;
  } else if (selected_index < s_cc_options.count) {
    m->cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }

  m->num_cc_numbers = 0;
  for (int i = 0; i < 4; i++) {
    if (m->cc_numbers[i] > 0) m->num_cc_numbers++;
  }

  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  if (!s_cc_options.options_str || s_cc_options.count == 0) load_cc_options();
  if (!s_cc_options.options_str) return NULL;

  uint8_t current_cc = m->cc_numbers[s_editing_cc_slot];
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
// Polarity Roller
// ============================================================================

static void polarity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  polarity_t polarities[] = { POLARITY_UNIPOLAR, POLARITY_BIPOLAR, POLARITY_INVERTED };
  if (selected_index < 3) {
    m->polarity = polarities[selected_index];
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* polarity_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  uint32_t current = 0;
  switch (m->polarity) {
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
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  curve_type_t curves[] = { CURVE_LINEAR, CURVE_EXPONENTIAL, CURVE_LOGARITHMIC, CURVE_S_CURVE };
  if (selected_index < 4) {
    m->curve.type = curves[selected_index];
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* curve_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  uint32_t current = 0;
  switch (m->curve.type) {
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
// Min / Middle / Max Rollers (CC output range anchors)
// ============================================================================

// Shared options buffer for 0..127 rollers. These rollers always navigate
// back to rebuild the axis page, so it's fine to reuse the buffer.
static char s_range_options[640];

static const char* build_range_options_0_127(void) {
  char* p = s_range_options;
  char* end = s_range_options + sizeof(s_range_options);
  for (int i = 0; i <= 127; i++) {
    int written = snprintf(p, end - p, (i == 0) ? "%d" : "\n%d", i);
    if (written <= 0 || written >= end - p) break;
    p += written;
  }
  return s_range_options;
}

static void min_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index <= 127) {
    m->min_value = (uint8_t)selected_index;
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* min_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;
  uint32_t current = m->min_value;
  if (current > 127) current = 0;
  return menu_create_roller_page("Min", build_range_options_0_127(), current,
    min_confirm_cb, NULL);
}

static void nav_to_min(void* user_data) {
  (void)user_data;
  menu_navigate_to("Min", min_roller_create);
}

static void middle_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index <= 127) {
    m->middle_value = (uint8_t)selected_index;
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* middle_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;
  uint32_t current = m->middle_value;
  if (current > 127) current = 64;
  return menu_create_roller_page("Middle", build_range_options_0_127(), current,
    middle_confirm_cb, NULL);
}

static void nav_to_middle(void* user_data) {
  (void)user_data;
  menu_navigate_to("Middle", middle_roller_create);
}

static void max_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index <= 127) {
    m->max_value = (uint8_t)selected_index;
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* max_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;
  uint32_t current = m->max_value;
  if (current > 127) current = 127;
  return menu_create_roller_page("Max", build_range_options_0_127(), current,
    max_confirm_cb, NULL);
}

static void nav_to_max(void* user_data) {
  (void)user_data;
  menu_navigate_to("Max", max_roller_create);
}

// ============================================================================
// Base Note Roller
// ============================================================================

static void base_note_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->base_note = (uint8_t)selected_index;
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* base_note_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  static char options[1536];
  options[0] = '\0';

  for (int i = 0; i < 128; i++) {
    char note[8];
    get_note_name(i, note, sizeof(note));
    if (i > 0) strcat(options, "\n");
    strcat(options, note);
  }

  uint32_t current = m->base_note;
  return menu_create_roller_page("Base Note", options, current, base_note_confirm_cb, NULL);
}

static void nav_to_base_note(void* user_data) {
  (void)user_data;
  menu_navigate_to("Base Note", base_note_roller_create);
}

// ============================================================================
// Range Roller
// ============================================================================

static void range_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->note_range = (uint8_t)((selected_index + 1) * 12);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* range_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  static const char* options = "1 Octave\n2 Octaves\n3 Octaves\n4 Octaves\n5 Octaves";
  uint32_t current = (m->note_range / 12);
  if (current > 0) current--;
  if (current > 4) current = 4;
  return menu_create_roller_page("Range", options, current, range_confirm_cb, NULL);
}

static void nav_to_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Range", range_roller_create);
}

// ============================================================================
// Velocity Mode Roller
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

  set_vel_mode(scene_get_current_index(), mode);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* velocity_mode_roller_create(void) {
  velocity_mode_t current = get_vel_mode(scene_get_current_index());

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
// Fixed Velocity Roller
// ============================================================================

static void velocity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  m->velocity = (uint8_t)(selected_index + 1);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* velocity_roller_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!m) return NULL;

  static char options[512];
  options[0] = '\0';
  for (int i = 1; i <= 127; i++) {
    char num[8];
    snprintf(num, sizeof(num), "%d", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, num);
  }

  uint8_t vel = m->velocity;
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
  set_nudge_pct(scene_get_current_index(), pct);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, axis_title(), menu_page_tilt_axis_scene_create);
}

static lv_obj_t* nudge_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 0; v <= 100; v += 5) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%%s", v, v < 100 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }

  uint8_t cur = get_nudge_pct(scene_get_current_index());
  if (cur > 100) cur = 100;
  uint32_t idx = cur / 5;
  return menu_create_roller_page("Nudge %", options, idx, nudge_confirm_cb, NULL);
}

static void nav_to_nudge(void* user_data) {
  (void)user_data;
  menu_navigate_to("Nudge %", nudge_roller_create);
}

// ============================================================================
// Custom back handler: rebuild the Tilt submenu on exit so it reflects any
// changes to the axis's enabled state, with focus restored to the axis the
// user was just editing.
// ============================================================================

static bool axis_back_handler(void) {
  // Only intercept when the axis page is the active screen; otherwise the
  // user is on a roller and default back handling should apply.
  if (s_axis_screen == NULL || lv_screen_active() != s_axis_screen) return false;

  menu_set_custom_back_handler(NULL);
  s_axis_screen = NULL;

  menu_set_restore_focus(s_axis == TILT_AXIS_X ? 0 : 1);
  menu_navigate_back_then_to(2, "Tilt", menu_page_tilt_scene_create);
  return true;
}

// ============================================================================
// Main Axis Page
// ============================================================================

lv_obj_t* menu_page_tilt_axis_scene_create(void) {
  scene_t* scene = scene_get_current();
  continuous_mapping_t* m = get_mapping(scene);
  if (!scene || !m) {
    lv_obj_t* empty = menu_create_page_2line(axis_title(), NULL, 0);
    s_axis_screen = empty;
    menu_set_custom_back_handler(axis_back_handler);
    return empty;
  }

  load_cc_options();

  int buf = get_next_buffer_set();
  int idx = 0;

  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

  snprintf(s_enabled_label[buf], sizeof(s_enabled_label[buf]), "%s\n%s",
    axis_title(), m->enabled ? "Enabled" : "Disabled");
  s_items[idx++] = (menu_item_t){s_enabled_label[buf], nav_to_enabled, NULL, true, MENU_ITEM_KIND_ROLLER};

  if (!m->enabled) {
    lv_obj_t* disabled_screen = menu_create_page_2line(axis_title(), s_items, idx);
    s_axis_screen = disabled_screen;
    menu_set_custom_back_handler(axis_back_handler);
    return disabled_screen;
  }

  snprintf(s_output_label[buf], sizeof(s_output_label[buf]), "Output\n%s",
    output_type_name(m->output_type));
  s_items[idx++] = (menu_item_t){
    s_output_label[buf], nav_to_output, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (m->output_type == OUTPUT_TYPE_CC) {
    for (int i = 0; i < 4; i++) {
      uint8_t cc_num = m->cc_numbers[i];
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
      s_items[idx++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true,
        MENU_ITEM_KIND_SUBMENU
      };
    }

    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(m->polarity));
    s_items[idx++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(m->curve.type));
    s_items[idx++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_min_label[buf], sizeof(s_min_label[buf]),
      "Min\n%u", (unsigned)m->min_value);
    s_items[idx++] = (menu_item_t){s_min_label[buf], nav_to_min, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_middle_label[buf], sizeof(s_middle_label[buf]),
      "Middle\n%u", (unsigned)m->middle_value);
    s_items[idx++] = (menu_item_t){s_middle_label[buf], nav_to_middle, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_max_label[buf], sizeof(s_max_label[buf]),
      "Max\n%u", (unsigned)m->max_value);
    s_items[idx++] = (menu_item_t){s_max_label[buf], nav_to_max, NULL, true, MENU_ITEM_KIND_ROLLER};

  } else if (m->output_type == OUTPUT_TYPE_NOTE) {
    char note_name[8];
    get_note_name(m->base_note, note_name, sizeof(note_name));
    snprintf(s_base_note_label[buf], sizeof(s_base_note_label[buf]),
      "Base Note\n%s", note_name);
    s_items[idx++] = (menu_item_t){s_base_note_label[buf], nav_to_base_note, NULL, true, MENU_ITEM_KIND_ROLLER};

    uint8_t octaves = m->note_range / 12;
    if (octaves == 0) octaves = 1;
    snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
      "Range\n%u Octave%s", (unsigned)octaves, octaves > 1 ? "s" : "");
    s_items[idx++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true, MENU_ITEM_KIND_ROLLER};

    velocity_mode_t vel_mode = get_vel_mode(scene_get_current_index());
    const char* vel_mode_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" :
      (vel_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Touchwheel";
    snprintf(s_velocity_mode_label[buf], sizeof(s_velocity_mode_label[buf]),
      "Velocity Mode\n%s", vel_mode_str);
    s_items[idx++] = (menu_item_t){s_velocity_mode_label[buf], nav_to_velocity_mode, NULL, true, MENU_ITEM_KIND_ROLLER};

    if (vel_mode == VELOCITY_MODE_FIXED) {
      uint8_t vel = m->velocity;
      if (vel == 0) vel = 100;
      snprintf(s_velocity_label[buf], sizeof(s_velocity_label[buf]),
        "Velocity\n%u", (unsigned)vel);
      s_items[idx++] = (menu_item_t){s_velocity_label[buf], nav_to_velocity, NULL, true, MENU_ITEM_KIND_ROLLER};
    }

    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(m->polarity));
    s_items[idx++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(m->curve.type));
    s_items[idx++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};

  } else if (m->output_type == OUTPUT_TYPE_LFO_RATE ||
             m->output_type == OUTPUT_TYPE_LFO_DEPTH) {
    snprintf(s_lfo_target_label[buf], sizeof(s_lfo_target_label[buf]),
      "LFO Target\n%s", lfo_target_to_string(m->lfo_target));
    s_items[idx++] = (menu_item_t){s_lfo_target_label[buf], nav_to_lfo_target, NULL, true, MENU_ITEM_KIND_ROLLER};

  } else if (m->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    uint8_t pct = get_nudge_pct(scene_get_current_index());
    snprintf(s_nudge_label[buf], sizeof(s_nudge_label[buf]),
      "Nudge %%\n%u%%", (unsigned)pct);
    s_items[idx++] = (menu_item_t){s_nudge_label[buf], nav_to_nudge, NULL, true, MENU_ITEM_KIND_ROLLER};

  } else if (m->output_type == OUTPUT_TYPE_PITCH_BEND) {
    // Pitch bend has no extra options on this page; polarity/curve still
    // influence the bipolar mapping.
    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(m->polarity));
    s_items[idx++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};

    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(m->curve.type));
    s_items[idx++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};
  }

  lv_obj_t* screen = menu_create_page_2line(axis_title(), s_items, idx);
  s_axis_screen = screen;
  menu_set_custom_back_handler(axis_back_handler);
  return screen;
}

void menu_page_tilt_axis_scene_cleanup(void) {
  free_cc_options();
}
