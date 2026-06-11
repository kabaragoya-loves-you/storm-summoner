#include "menu.h"
#include "menu_pages.h"
#include "action_config.h"
#include "action.h"
#include "cv.h"
#include "scene.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "device_config.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "tempo.h"
#include "ui.h"
#include "audio_calibrate.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define TAG "MENU_CV_SCENE"

// Forward declarations
lv_obj_t* menu_page_cv_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_CV_ITEMS 20
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

// Audio mode labels
static char s_audio_calibrate_label[LABEL_BUFFER_SETS][32];
static char s_audio_range_label[LABEL_BUFFER_SETS][32];
static char s_audio_sensitivity_label[LABEL_BUFFER_SETS][48];  // Larger for gain display
static char s_audio_attack_label[LABEL_BUFFER_SETS][32];
static char s_audio_release_label[LABEL_BUFFER_SETS][32];
static char s_audio_threshold_label[LABEL_BUFFER_SETS][32];
static char s_audio_polarity_label[LABEL_BUFFER_SETS][32];

// LFO modulation labels
static char s_lfo_target_label[LABEL_BUFFER_SETS][32];
static char s_nudge_label[LABEL_BUFFER_SETS][32];

// Trigger mode labels
static char s_trigger_action_label[LABEL_BUFFER_SETS][64];
static char s_trigger_threshold_label[LABEL_BUFFER_SETS][32];
static char s_trigger_debounce_label[LABEL_BUFFER_SETS][32];
static action_config_context_t s_trigger_action_ctx;

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
    case INPUT_MODE_CV: return "Control Voltage";
    case INPUT_MODE_CLOCK_SYNC: return "Clock Sync";
    case INPUT_MODE_AUDIO: return "Audio";
    case INPUT_MODE_NOTE: return "CV/Gate";
    case INPUT_MODE_TRIGGER: return "Trigger";
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
  // 0=None, 1=Control Voltage, 2=CV/Gate, 3=Audio, 4=Trigger
  input_mode_t modes[] = {
    INPUT_MODE_NONE,
    INPUT_MODE_CV,
    INPUT_MODE_NOTE,
    INPUT_MODE_AUDIO,
    INPUT_MODE_TRIGGER
  };
  
  if (selected_index >= 5) {
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* mode_roller_create(void) {
  static const char* options = "<None>\nControl Voltage\nCV/Gate\nAudio\nTrigger";
  
  uint8_t scene_index = scene_get_current_index();
  input_mode_t current = scene_get_cv_input_mode(scene_index);
  
  uint32_t current_idx = 0;
  switch (current) {
    case INPUT_MODE_NONE: current_idx = 0; break;
    case INPUT_MODE_CV: current_idx = 1; break;
    case INPUT_MODE_NOTE: current_idx = 2; break;
    case INPUT_MODE_AUDIO: current_idx = 3; break;
    case INPUT_MODE_TRIGGER: current_idx = 4; break;
    default: current_idx = 0; break;
  }
  
  return menu_create_roller_page("Mode", options, current_idx, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Trigger Mode: Action, Threshold, Debounce
// ============================================================================

static void trigger_action_complete(action_config_context_t* ctx, action_t* action) {
  (void)ctx;
  (void)action;
  persist_scene_changes();
}

static void fill_trigger_action_ctx(void) {
  uint8_t scene_index = scene_get_current_index();
  action_t* target = scene_get_cv_trigger_action(scene_index);
  if (!target) return;

  s_trigger_action_ctx.target_action = target;
  s_trigger_action_ctx.return_page = menu_page_cv_scene_create;
  s_trigger_action_ctx.return_depth = 1;
  s_trigger_action_ctx.type_picker_pop_depth = 0;
  s_trigger_action_ctx.on_complete = trigger_action_complete;
  s_trigger_action_ctx.user_data = NULL;
  s_trigger_action_ctx.trigger_type = ACTION_TRIGGER_CC;
  s_trigger_action_ctx.detail_title = "CV Trigger";
  s_trigger_action_ctx.source_title = "CV Trigger";
}

static void nav_to_trigger_action(void* user_data) {
  (void)user_data;
  action_t* target = scene_get_cv_trigger_action(scene_get_current_index());
  if (!target) return;

  fill_trigger_action_ctx();
  if (target->type == ACTION_NONE) {
    s_trigger_action_ctx.type_picker_pop_depth = 1;
    action_config_start_type_picker(&s_trigger_action_ctx);
  } else {
    action_config_start(&s_trigger_action_ctx);
  }
}

static void trigger_threshold_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  scene_set_cv_trigger_threshold(scene_get_current_index(), (uint8_t)selected_index);
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* trigger_threshold_roller_create(void) {
  static char options[512];
  options[0] = '\0';
  for (int i = 0; i <= 100; i++) {
    char line[16];
    snprintf(line, sizeof(line), "%d%%", i);
    if (i > 0) strcat(options, "\n");
    strcat(options, line);
  }

  uint8_t current = scene_get_cv_trigger_threshold(scene_get_current_index());
  return menu_create_roller_page("Threshold", options, current, trigger_threshold_confirm_cb, NULL);
}

static void nav_to_trigger_threshold(void* user_data) {
  (void)user_data;
  menu_navigate_to("Threshold", trigger_threshold_roller_create);
}

static const uint16_t TRIGGER_DEBOUNCE_VALUES[] = {
  0, 50, 100, 200, 300, 500, 750, 1000, 1500, 2000
};
static const char* TRIGGER_DEBOUNCE_OPTIONS =
  "Immediate\n50ms\n100ms\n200ms\n300ms\n500ms\n750ms\n1s\n1.5s\n2s";

static uint32_t trigger_debounce_index_for_ms(uint16_t ms) {
  for (uint32_t i = 0; i < sizeof(TRIGGER_DEBOUNCE_VALUES) / sizeof(TRIGGER_DEBOUNCE_VALUES[0]); i++) {
    if (TRIGGER_DEBOUNCE_VALUES[i] == ms) return i;
  }
  return 0;
}

static void trigger_debounce_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (selected_index >= sizeof(TRIGGER_DEBOUNCE_VALUES) / sizeof(TRIGGER_DEBOUNCE_VALUES[0])) return;
  scene_set_cv_trigger_debounce_ms(scene_get_current_index(),
    TRIGGER_DEBOUNCE_VALUES[selected_index]);
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* trigger_debounce_roller_create(void) {
  uint16_t current = scene_get_cv_trigger_debounce_ms(scene_get_current_index());
  uint32_t idx = trigger_debounce_index_for_ms(current);
  return menu_create_roller_page("Debounce", TRIGGER_DEBOUNCE_OPTIONS, idx,
    trigger_debounce_confirm_cb, NULL);
}

static void nav_to_trigger_debounce(void* user_data) {
  (void)user_data;
  menu_navigate_to("Debounce", trigger_debounce_roller_create);
}

static void format_trigger_debounce_label(char* buf, size_t len, uint16_t ms) {
  if (ms == 0) {
    snprintf(buf, len, "Immediate");
    return;
  }
  if (ms % 1000 == 0) {
    snprintf(buf, len, "%us", (unsigned)(ms / 1000));
    return;
  }
  if (ms == 1500) {
    snprintf(buf, len, "1.5s");
    return;
  }
  snprintf(buf, len, "%ums", (unsigned)ms);
}

// ============================================================================
// Output Type Roller (CC vs Notes) - For Control Voltage Mode
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
  // 0=CC, 1=Note, 2=LFO Rate, 3=LFO Depth, 4=Tempo Nudge
  switch (selected_index) {
    case 0: scene->cv.output_type = OUTPUT_TYPE_CC; break;
    case 1: scene->cv.output_type = OUTPUT_TYPE_NOTE; break;
    case 2: scene->cv.output_type = OUTPUT_TYPE_LFO_RATE; break;
    case 3: scene->cv.output_type = OUTPUT_TYPE_LFO_DEPTH; break;
    case 4: scene->cv.output_type = OUTPUT_TYPE_TEMPO_NUDGE; break;
    default: scene->cv.output_type = OUTPUT_TYPE_CC; break;
  }
  persist_scene_changes();
  
  ESP_LOGI(TAG, "CV output set to type %d", selected_index);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* output_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  // Map output type to roller index
  uint32_t current = 0;
  switch (scene->cv.output_type) {
    case OUTPUT_TYPE_CC: current = 0; break;
    case OUTPUT_TYPE_NOTE: current = 1; break;
    case OUTPUT_TYPE_LFO_RATE: current = 2; break;
    case OUTPUT_TYPE_LFO_DEPTH: current = 3; break;
    case OUTPUT_TYPE_TEMPO_NUDGE: current = 4; break;
    default: current = 0; break;
  }
  return menu_create_roller_page("Output",
    "Control Change\nNotes\nLFO Rate\nLFO Depth\nTempo Nudge",
    current, output_confirm_cb, NULL);
}

static void nav_to_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Output", output_roller_create);
}

// ============================================================================
// LFO Target Roller (for LFO Rate/Depth output modes)
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
  
  scene->cv.lfo_target = (lfo_target_t)selected_index;
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (uint32_t)scene->cv.lfo_target;
  return menu_create_roller_page("LFO Target", "LFO1\nLFO2\nBoth", current,
    lfo_target_confirm_cb, NULL);
}

static void nav_to_lfo_target(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO Target", lfo_target_roller_create);
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
  scene_set_cv_tempo_nudge_pct(scene_get_current_index(), pct);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* nudge_roller_create(void) {
  static char options[256];
  options[0] = '\0';
  for (int v = 0; v <= 100; v += 5) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%%s", v, v < 100 ? "\n" : "");
    strncat(options, buf, sizeof(options) - strlen(options) - 1);
  }

  uint8_t cur = scene_get_cv_tempo_nudge_pct(scene_get_current_index());
  if (cur > 100) cur = 100;
  uint32_t idx = cur / 5;
  return menu_create_roller_page("Nudge %", options, idx, nudge_confirm_cb, NULL);
}

static void nav_to_nudge(void* user_data) {
  (void)user_data;
  menu_navigate_to("Nudge %", nudge_roller_create);
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
// Base Note Roller (for Control Voltage Notes Output)
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
// Range Roller (for Control Voltage Notes Output)
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
// Velocity Roller (for Control Voltage Notes Output)
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
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
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
// CV/Gate Mode Velocity Settings (for INPUT_MODE_NOTE)
// ============================================================================

static void note_vel_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t scene_index = scene_get_current_index();
  velocity_mode_t mode = (selected_index == 1) ? VELOCITY_MODE_GATE_VOLTAGE : VELOCITY_MODE_FIXED;
  
  scene_set_cv_velocity_mode(scene_index, mode);
  
  const char* mode_str = (mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Fixed";
  ESP_LOGI(TAG, "CV velocity mode set to: %s", mode_str);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* note_vel_mode_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  velocity_mode_t current = scene_get_cv_velocity_mode(scene_index);
  uint32_t current_idx = (current == VELOCITY_MODE_GATE_VOLTAGE) ? 1 : 0;
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
  
  scene_set_cv_velocity(scene_index, velocity);
  
  ESP_LOGI(TAG, "CV fixed velocity set to: %d", velocity);
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* note_fixed_vel_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  uint8_t current_vel = scene_get_cv_velocity(scene_index);
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
// Audio Mode Rollers
// ============================================================================

// Audio Range Roller
static void audio_range_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t scene_index = scene_get_current_index();
  cv_range_t range = (selected_index == 0) ? CV_RANGE_BIPOLAR_5V : CV_RANGE_BIPOLAR_10V;
  scene_set_audio_range(scene_index, range);
  
  // Update running audio mode if active
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_range_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  cv_range_t current = scene_get_audio_range(scene_index);
  uint32_t current_idx = (current == CV_RANGE_BIPOLAR_10V) ? 1 : 0;
  return menu_create_roller_page("Range", "+-5V\n+-10V", current_idx, audio_range_confirm_cb, NULL);
}

static void nav_to_audio_range(void* user_data) {
  (void)user_data;
  menu_navigate_to("Range", audio_range_roller_create);
}

// Audio Calibrate Action
static void audio_calibrate_action(void* user_data) {
  (void)user_data;
  if (!cv_is_audio_mode_active()) {
    ESP_LOGW(TAG, "Audio mode must be active to calibrate");
    return;
  }
  
  // Switch to calibration UI with visual feedback
  ui_set_draw_module(&audio_calibrate_module);
}

// Audio Sensitivity Roller - integer gain values 1x to 64x
#define NUM_GAIN_VALUES 64

static uint8_t gain_to_sensitivity(int gain) {
  // sensitivity = 255 * log(gain * 4) / log(256)
  if (gain < 1) gain = 1;
  if (gain > 64) gain = 64;
  float sens = 255.0f * logf((float)gain * 4.0f) / logf(256.0f);
  if (sens < 0) sens = 0;
  if (sens > 255) sens = 255;
  return (uint8_t)(sens + 0.5f);
}

static int find_closest_gain_index(float target) {
  // Gain values are 1-64, index 0 = 1x, index 63 = 64x
  int closest = (int)(target + 0.5f) - 1;
  if (closest < 0) closest = 0;
  if (closest >= NUM_GAIN_VALUES) closest = NUM_GAIN_VALUES - 1;
  return closest;
}

static void audio_sensitivity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  // Index 0 = 1x, index 63 = 64x
  int gain = (int)selected_index + 1;
  if (gain > 64) gain = 64;
  uint8_t sens = gain_to_sensitivity(gain);
  
  uint8_t scene_index = scene_get_current_index();
  scene_set_audio_sensitivity(scene_index, sens);
  
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_set_restore_focus(1);  // Sensitivity is item 1
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_sensitivity_roller_create(void) {
  // Build options string: 1x, 2x, 3x, ... 64x
  static char options[512];
  options[0] = '\0';
  for (int i = 1; i <= NUM_GAIN_VALUES; i++) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%dx", i);
    if (i > 1) strcat(options, "\n");
    strcat(options, buf);
  }
  
  uint8_t scene_index = scene_get_current_index();
  uint8_t sens = scene_get_audio_sensitivity(scene_index);
  float current_gain = 0.25f * powf(256.0f, sens / 255.0f);
  uint32_t current_idx = (uint32_t)find_closest_gain_index(current_gain);
  
  return menu_create_roller_page("Sensitivity", options, current_idx, audio_sensitivity_confirm_cb, NULL);
}

static void nav_to_audio_sensitivity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Sensitivity", audio_sensitivity_roller_create);
}

// Audio Threshold Roller (0-127 integers)
static void audio_threshold_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint8_t threshold = (uint8_t)selected_index;
  if (threshold > 127) threshold = 127;
  
  uint8_t scene_index = scene_get_current_index();
  scene_set_audio_threshold(scene_index, threshold);
  
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_set_restore_focus(2);  // Threshold is item 2
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_threshold_roller_create(void) {
  // Build options string 0-127
  static char options[1024];
  options[0] = '\0';
  strcpy(options, "Off");
  for (int i = 1; i <= 127; i++) {
    char buf[8];
    snprintf(buf, sizeof(buf), "\n%d", i);
    strcat(options, buf);
  }
  
  uint8_t scene_index = scene_get_current_index();
  uint8_t current = scene_get_audio_threshold(scene_index);
  
  return menu_create_roller_page("Threshold", options, current, audio_threshold_confirm_cb, NULL);
}

static void nav_to_audio_threshold(void* user_data) {
  (void)user_data;
  menu_navigate_to("Threshold", audio_threshold_roller_create);
}

// Audio Attack Roller
static void audio_attack_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint16_t attack_values[] = {5, 10, 20, 30, 50, 75, 100};
  uint16_t attack = attack_values[selected_index < 7 ? selected_index : 1];
  
  uint8_t scene_index = scene_get_current_index();
  scene_set_audio_attack(scene_index, attack);
  
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_attack_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  uint16_t attack = scene_get_audio_attack(scene_index);
  
  uint32_t current_idx = 1;  // Default to 10ms
  if (attack <= 5) current_idx = 0;
  else if (attack <= 10) current_idx = 1;
  else if (attack <= 20) current_idx = 2;
  else if (attack <= 30) current_idx = 3;
  else if (attack <= 50) current_idx = 4;
  else if (attack <= 75) current_idx = 5;
  else current_idx = 6;
  
  return menu_create_roller_page("Attack", "5ms\n10ms\n20ms\n30ms\n50ms\n75ms\n100ms", 
    current_idx, audio_attack_confirm_cb, NULL);
}

static void nav_to_audio_attack(void* user_data) {
  (void)user_data;
  menu_navigate_to("Attack", audio_attack_roller_create);
}

// Audio Release Roller
static void audio_release_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  uint16_t release_values[] = {50, 100, 200, 300, 500, 750, 1000, 1500, 2000};
  uint16_t release = release_values[selected_index < 9 ? selected_index : 2];
  
  uint8_t scene_index = scene_get_current_index();
  scene_set_audio_release(scene_index, release);
  
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_release_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  uint16_t release = scene_get_audio_release(scene_index);
  
  uint32_t current_idx = 2;  // Default to 200ms
  if (release <= 50) current_idx = 0;
  else if (release <= 100) current_idx = 1;
  else if (release <= 200) current_idx = 2;
  else if (release <= 300) current_idx = 3;
  else if (release <= 500) current_idx = 4;
  else if (release <= 750) current_idx = 5;
  else if (release <= 1000) current_idx = 6;
  else if (release <= 1500) current_idx = 7;
  else current_idx = 8;
  
  return menu_create_roller_page("Release", "50ms\n100ms\n200ms\n300ms\n500ms\n750ms\n1000ms\n1500ms\n2000ms", 
    current_idx, audio_release_confirm_cb, NULL);
}

static void nav_to_audio_release(void* user_data) {
  (void)user_data;
  menu_navigate_to("Release", audio_release_roller_create);
}

// Audio Polarity Roller
static void audio_polarity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  audio_polarity_t polarity = (selected_index == 0) ? AUDIO_POLARITY_ATTRACT : AUDIO_POLARITY_REPEL;
  
  uint8_t scene_index = scene_get_current_index();
  scene_set_audio_polarity(scene_index, polarity);
  
  if (cv_is_audio_mode_active()) {
    audio_config_t* cfg = scene_get_audio_config(scene_index);
    cv_update_audio_config(cfg);
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Control Voltage", menu_page_cv_scene_create);
}

static lv_obj_t* audio_polarity_roller_create(void) {
  uint8_t scene_index = scene_get_current_index();
  audio_polarity_t polarity = scene_get_audio_polarity(scene_index);
  uint32_t current_idx = (polarity == AUDIO_POLARITY_REPEL) ? 1 : 0;
  return menu_create_roller_page("Polarity", "Attract\nRepel (Duck)", current_idx, audio_polarity_confirm_cb, NULL);
}

static void nav_to_audio_polarity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Polarity", audio_polarity_roller_create);
}

// ============================================================================
// Main CV Scene Page
// ============================================================================

lv_obj_t* menu_page_cv_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("Control Voltage", NULL, 0);
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
    s_cv_items[item_count++] = (menu_item_t){s_mode_label[buf], NULL, NULL, false, MENU_ITEM_KIND_DISPLAY};
    
    // Show info that CV is controlled by tempo
    return menu_create_page_2line("Control Voltage", s_cv_items, item_count);
  }

  snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\n%s",
    get_cv_mode_display_name(mode));
  s_cv_items[item_count++] = (menu_item_t){
    s_mode_label[buf], nav_to_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  // Mode-specific items
  switch (mode) {
    case INPUT_MODE_CV: {
      // Control Voltage mode: Output selector (CC, Notes, LFO Rate, LFO Depth, Tempo Nudge)
      const char* output_name;
      switch (scene->cv.output_type) {
        case OUTPUT_TYPE_CC: output_name = "Control Change"; break;
        case OUTPUT_TYPE_NOTE: output_name = "Notes"; break;
        case OUTPUT_TYPE_LFO_RATE: output_name = "LFO Rate"; break;
        case OUTPUT_TYPE_LFO_DEPTH: output_name = "LFO Depth"; break;
        case OUTPUT_TYPE_TEMPO_NUDGE: output_name = "Tempo Nudge"; break;
        default: output_name = "Control Change"; break;
      }
      snprintf(s_output_label[buf], sizeof(s_output_label[buf]), "Output\n%s", output_name);
      s_cv_items[item_count++] = (menu_item_t){s_output_label[buf], nav_to_output, NULL, true, MENU_ITEM_KIND_ROLLER};
      
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
            s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true,
            MENU_ITEM_KIND_SUBMENU
          };
        }
        
        // Polarity (envelope shaping)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->cv.polarity));
        s_cv_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        // Curve
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->cv.curve.type));
        s_cv_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};
        
      } else if (scene->cv.output_type == OUTPUT_TYPE_NOTE) {
        // Notes output mode: Base Note, Range, Velocity
        char note_name[8];
        get_note_name(scene->cv.base_note, note_name, sizeof(note_name));
        snprintf(s_base_note_label[buf], sizeof(s_base_note_label[buf]),
          "Base Note\n%s", note_name);
        s_cv_items[item_count++] = (menu_item_t){s_base_note_label[buf], nav_to_base_note, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        uint8_t octaves = scene->cv.note_range / 12;
        if (octaves == 0) octaves = 1;
        snprintf(s_range_label[buf], sizeof(s_range_label[buf]),
          "Range\n%u Octave%s", (unsigned)octaves, octaves > 1 ? "s" : "");
        s_cv_items[item_count++] = (menu_item_t){s_range_label[buf], nav_to_range, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        uint8_t vel = scene->cv.velocity;
        if (vel == 0) vel = 100;
        snprintf(s_velocity_label[buf], sizeof(s_velocity_label[buf]),
          "Velocity\n%u", (unsigned)vel);
        s_cv_items[item_count++] = (menu_item_t){s_velocity_label[buf], nav_to_velocity, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        // Polarity (envelope shaping - also applies to notes)
        snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
          "Polarity\n%s", polarity_to_string(scene->cv.polarity));
        s_cv_items[item_count++] = (menu_item_t){s_polarity_label[buf], nav_to_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
        
        // Curve (also applies to notes)
        snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
          "Curve\n%s", curve_type_to_string(scene->cv.curve.type));
        s_cv_items[item_count++] = (menu_item_t){s_curve_label[buf], nav_to_curve, NULL, true, MENU_ITEM_KIND_ROLLER};
      } else if (scene->cv.output_type == OUTPUT_TYPE_LFO_RATE ||
                 scene->cv.output_type == OUTPUT_TYPE_LFO_DEPTH) {
        // LFO modulation mode: Target selector
        snprintf(s_lfo_target_label[buf], sizeof(s_lfo_target_label[buf]),
          "LFO Target\n%s", lfo_target_to_string(scene->cv.lfo_target));
        s_cv_items[item_count++] = (menu_item_t){s_lfo_target_label[buf], nav_to_lfo_target, NULL, true, MENU_ITEM_KIND_ROLLER};
      } else if (scene->cv.output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
        uint8_t pct = scene_get_cv_tempo_nudge_pct(scene_index);
        snprintf(s_nudge_label[buf], sizeof(s_nudge_label[buf]),
          "Nudge %%\n%u%%", (unsigned)pct);
        s_cv_items[item_count++] = (menu_item_t){s_nudge_label[buf], nav_to_nudge, NULL, true, MENU_ITEM_KIND_ROLLER};
      }
      
      break;
    }
    
    case INPUT_MODE_NOTE: {
      // CV/Gate Mode: CV pitch + Expression gate
      // Show velocity mode
      velocity_mode_t vel_mode = scene_get_cv_velocity_mode(scene_index);
      const char* vel_mode_str = (vel_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "Gate Voltage" : "Fixed";
      snprintf(s_vel_mode_label[buf], sizeof(s_vel_mode_label[buf]),
        "Velocity Mode\n%s", vel_mode_str);
      s_cv_items[item_count++] = (menu_item_t){s_vel_mode_label[buf], nav_to_note_vel_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Show fixed velocity value if in fixed mode
      if (vel_mode == VELOCITY_MODE_FIXED) {
        uint8_t fixed_vel = scene_get_cv_velocity(scene_index);
        if (fixed_vel == 0) fixed_vel = 100;
        snprintf(s_fixed_vel_label[buf], sizeof(s_fixed_vel_label[buf]),
          "Fixed Velocity\n%u", (unsigned)fixed_vel);
        s_cv_items[item_count++] = (menu_item_t){s_fixed_vel_label[buf], nav_to_note_fixed_vel, NULL, true, MENU_ITEM_KIND_ROLLER};
      }
      
      // Show info that Expression is locked to Gate mode
      snprintf(s_gate_info_label[buf], sizeof(s_gate_info_label[buf]),
        "Expression\nLocked to Gate");
      s_cv_items[item_count++] = (menu_item_t){s_gate_info_label[buf], NULL, NULL, false, MENU_ITEM_KIND_DISPLAY};
      
      break;
    }

    case INPUT_MODE_TRIGGER: {
      char action_name[48];
      action_t* trigger_action = scene_get_cv_trigger_action(scene_index);
      if (trigger_action && trigger_action->type != ACTION_NONE)
        action_get_display_name(trigger_action, action_name, sizeof(action_name));
      else
        snprintf(action_name, sizeof(action_name), "None");

      snprintf(s_trigger_action_label[buf], sizeof(s_trigger_action_label[buf]),
        "Action\n%s", action_name);
      s_cv_items[item_count++] = (menu_item_t){
        s_trigger_action_label[buf], nav_to_trigger_action, NULL, true, MENU_ITEM_KIND_ROLLER
      };

      snprintf(s_trigger_threshold_label[buf], sizeof(s_trigger_threshold_label[buf]),
        "Threshold\n%u%%", (unsigned)scene_get_cv_trigger_threshold(scene_index));
      s_cv_items[item_count++] = (menu_item_t){
        s_trigger_threshold_label[buf], nav_to_trigger_threshold, NULL, true, MENU_ITEM_KIND_ROLLER
      };

      char debounce_str[16];
      format_trigger_debounce_label(debounce_str, sizeof(debounce_str),
        scene_get_cv_trigger_debounce_ms(scene_index));
      snprintf(s_trigger_debounce_label[buf], sizeof(s_trigger_debounce_label[buf]),
        "Debounce\n%s", debounce_str);
      s_cv_items[item_count++] = (menu_item_t){
        s_trigger_debounce_label[buf], nav_to_trigger_debounce, NULL, true, MENU_ITEM_KIND_ROLLER
      };
      break;
    }
    
    case INPUT_MODE_NONE:
      // No additional menu items when disabled
      break;
    
    case INPUT_MODE_AUDIO: {
      // Audio envelope follower mode
      
      // Calibrate action (first and most important)
      snprintf(s_audio_calibrate_label[buf], sizeof(s_audio_calibrate_label[buf]), "Calibrate\nPlay Loud!");
      s_cv_items[item_count++] = (menu_item_t){s_audio_calibrate_label[buf], audio_calibrate_action, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Sensitivity - editable roller
      uint8_t sens = scene_get_audio_sensitivity(scene_index);
      float gain = 0.25f * powf(256.0f, sens / 255.0f);
      if (gain >= 10.0f) {
        snprintf(s_audio_sensitivity_label[buf], sizeof(s_audio_sensitivity_label[buf]), 
          "Sensitivity\n%.0fx", gain);
      } else {
        snprintf(s_audio_sensitivity_label[buf], sizeof(s_audio_sensitivity_label[buf]), 
          "Sensitivity\n%.1fx", gain);
      }
      s_cv_items[item_count++] = (menu_item_t){s_audio_sensitivity_label[buf], nav_to_audio_sensitivity, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Threshold - editable roller
      uint8_t thresh = scene_get_audio_threshold(scene_index);
      if (thresh == 0) {
        snprintf(s_audio_threshold_label[buf], sizeof(s_audio_threshold_label[buf]), "Threshold\nOff");
      } else {
        snprintf(s_audio_threshold_label[buf], sizeof(s_audio_threshold_label[buf]), "Threshold\n%u", (unsigned)thresh);
      }
      s_cv_items[item_count++] = (menu_item_t){s_audio_threshold_label[buf], nav_to_audio_threshold, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Range (±5V or ±10V) - editable
      cv_range_t audio_range = scene_get_audio_range(scene_index);
      const char* range_str = (audio_range == CV_RANGE_BIPOLAR_10V) ? "+-10V" : "+-5V";
      snprintf(s_audio_range_label[buf], sizeof(s_audio_range_label[buf]), "Range\n%s", range_str);
      s_cv_items[item_count++] = (menu_item_t){s_audio_range_label[buf], nav_to_audio_range, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Attack - editable
      uint16_t attack = scene_get_audio_attack(scene_index);
      snprintf(s_audio_attack_label[buf], sizeof(s_audio_attack_label[buf]), "Attack\n%ums", attack);
      s_cv_items[item_count++] = (menu_item_t){s_audio_attack_label[buf], nav_to_audio_attack, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Release - editable
      uint16_t release = scene_get_audio_release(scene_index);
      snprintf(s_audio_release_label[buf], sizeof(s_audio_release_label[buf]), "Release\n%ums", release);
      s_cv_items[item_count++] = (menu_item_t){s_audio_release_label[buf], nav_to_audio_release, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // Polarity (Attract/Repel) - editable
      audio_polarity_t polarity = scene_get_audio_polarity(scene_index);
      const char* pol_str = (polarity == AUDIO_POLARITY_REPEL) ? "Repel (Duck)" : "Attract";
      snprintf(s_audio_polarity_label[buf], sizeof(s_audio_polarity_label[buf]), "Polarity\n%s", pol_str);
      s_cv_items[item_count++] = (menu_item_t){s_audio_polarity_label[buf], nav_to_audio_polarity, NULL, true, MENU_ITEM_KIND_ROLLER};
      
      // CC slots (reuse existing CC slot functionality)
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
          s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true,
          MENU_ITEM_KIND_SUBMENU
        };
      }
      break;
    }
    
    case INPUT_MODE_CLOCK_SYNC:
      // This case is handled above (clock_sync_active)
      break;
    
    default:
      break;
  }
  
  return menu_create_page_2line("Control Voltage", s_cv_items, item_count);
}

// Cleanup function
void menu_page_cv_scene_cleanup(void) {
  free_cc_options();
}

