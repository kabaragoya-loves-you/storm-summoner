#include "menu.h"
#include "menu_pages.h"
#include "lfo.h"
#include "scene.h"
#include "continuous_mapping.h"
#include "rtg.h"
#include "sample_hold.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "ui.h"
#include "midi_lfo_scene_handler.h"
#include "lfo_mode_mapping.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_LFO1_SCENE"

// Forward declarations
lv_obj_t* menu_page_lfo1_scene_create(void);

// ============================================================================
// Static Storage
// ============================================================================

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_LFO_ITEMS 18
static menu_item_t s_lfo_items[MAX_LFO_ITEMS];

static char s_start_mode_label[LABEL_BUFFER_SETS][32];
static char s_trigger_timing_label[LABEL_BUFFER_SETS][32];
static char s_repeat_label[LABEL_BUFFER_SETS][32];
static char s_phase_reset_label[LABEL_BUFFER_SETS][32];
static char s_restore_on_stop_label[LABEL_BUFFER_SETS][32];
static char s_waveform_label[LABEL_BUFFER_SETS][32];
static char s_rate_mode_label[LABEL_BUFFER_SETS][32];
static char s_rate_label[LABEL_BUFFER_SETS][32];
static char s_division_label[LABEL_BUFFER_SETS][32];
static char s_mode_label[LABEL_BUFFER_SETS][32];
static char s_cc_slot_labels[LABEL_BUFFER_SETS][4][48];
static char s_floor_label[LABEL_BUFFER_SETS][32];
static char s_ceiling_label[LABEL_BUFFER_SETS][32];
static char s_resolution_label[LABEL_BUFFER_SETS][32];
static char s_steps_label[LABEL_BUFFER_SETS][32];

// CC options from device
typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};
static uint8_t s_editing_cc_slot = 0;
static bool s_callback_in_progress = false;

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

static const char* waveform_to_display_string(lfo_waveform_t wf) {
  switch (wf) {
    case LFO_WAVEFORM_SINE: return "Sine";
    case LFO_WAVEFORM_TRIANGLE: return "Triangle";
    case LFO_WAVEFORM_SQUARE: return "Square";
    case LFO_WAVEFORM_SAW_UP: return "Saw Up";
    case LFO_WAVEFORM_SAW_DOWN: return "Saw Down";
    case LFO_WAVEFORM_SAMPLE_HOLD: return "S&H";
    case LFO_WAVEFORM_BIN: return "Bin";
    case LFO_WAVEFORM_GLIDER: return "Glider";
    case LFO_WAVEFORM_STRAY: return "Stray";
    case LFO_WAVEFORM_CUSTOM: return "Custom";
    default: return "Unknown";
  }
}

static const char* division_to_display_string(lfo_note_division_t div) {
  switch (div) {
    case LFO_DIVISION_16_BARS: return "16 Bars";
    case LFO_DIVISION_12_BARS: return "12 Bars";
    case LFO_DIVISION_8_BARS: return "8 Bars";
    case LFO_DIVISION_4_BARS: return "4 Bars";
    case LFO_DIVISION_2_BARS: return "2 Bars";
    case LFO_DIVISION_1_BAR: return "1 Bar";
    case LFO_DIVISION_HALF: return "1/2 Note";
    case LFO_DIVISION_QUARTER: return "1/4 Note";
    case LFO_DIVISION_EIGHTH: return "1/8 Note";
    case LFO_DIVISION_SIXTEENTH: return "1/16 Note";
    case LFO_DIVISION_32ND: return "1/32 Note";
    default: return "Unknown";
  }
}

static const char* start_mode_to_display_string(lfo_start_mode_t mode) {
  switch (mode) {
    case LFO_START_RUNNING: return "Running";
    case LFO_START_PAUSED: return "Paused";
    case LFO_START_TRANSPORT: return "Follow Transport";
    default: return "Running";
  }
}

static const char* trigger_timing_to_display_string(lfo_trigger_timing_t timing) {
  switch (timing) {
    case LFO_TRIGGER_IMMEDIATE: return "Immediate";
    case LFO_TRIGGER_NEXT_BEAT: return "Next Beat";
    case LFO_TRIGGER_NEXT_BAR: return "Next Bar";
    default: return "Immediate";
  }
}

static const char* rate_mode_to_display_string(lfo_rate_mode_t mode) {
  return (mode == LFO_RATE_MODE_TEMPO) ? "Division" : "Time";
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

  const lfo_mode_mapping_t* mapping = lfo_get_mode_mapping(0, (uint8_t)selected_index);
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

  bool was_enabled = scene->lfo1_config.enabled;
  bool will_be_enabled = mapping->enabled;

  if (was_enabled && !will_be_enabled)
    midi_lfo_scene_handler_release_notes_for_slot(0);

  if (will_be_enabled) {
    output_type_t prev_type = scene->lfo1.output_type;
    scene->lfo1.output_type = mapping->output_type;
    if (prev_type == OUTPUT_TYPE_RTG_RATE && scene->lfo1.output_type != OUTPUT_TYPE_RTG_RATE)
      rtg_clear_dynamic_rate();
    if (prev_type == OUTPUT_TYPE_SH_RATE && scene->lfo1.output_type != OUTPUT_TYPE_SH_RATE)
      sample_hold_clear_dynamic_rate();
  }

  scene->lfo1_config.enabled = will_be_enabled;
  scene->lfo1.enabled = will_be_enabled;
  lfo_apply_config(0, &scene->lfo1_config);
  persist_scene_changes();

  ESP_LOGI(TAG, "LFO1 mode set to: %s", mapping->display_name);

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* mode_roller_create(void) {
  static char options[384];
  options[0] = '\0';
  for (uint8_t i = 0; i < NUM_LFO_USER_MODES; i++) {
    if (i > 0) strcat(options, "\n");
    strcat(options, lfo_get_mode_name(0, i));
  }

  uint32_t current_idx = lfo_get_current_mode_index(0, scene_get_current());
  return menu_create_roller_page("Mode", options, current_idx, mode_confirm_cb, NULL);
}

static void nav_to_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Mode", mode_roller_create);
}

// ============================================================================
// Waveform Roller
// ============================================================================

static void waveform_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  lfo_waveform_t waveforms[] = {
    LFO_WAVEFORM_SINE, LFO_WAVEFORM_TRIANGLE, LFO_WAVEFORM_SQUARE,
    LFO_WAVEFORM_SAW_UP, LFO_WAVEFORM_SAW_DOWN, LFO_WAVEFORM_SAMPLE_HOLD,
    LFO_WAVEFORM_BIN, LFO_WAVEFORM_GLIDER, LFO_WAVEFORM_STRAY
  };

  if (selected_index < 9) {
    scene->lfo1_config.waveform = waveforms[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* waveform_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->lfo1_config.waveform) {
    case LFO_WAVEFORM_SINE: current = 0; break;
    case LFO_WAVEFORM_TRIANGLE: current = 1; break;
    case LFO_WAVEFORM_SQUARE: current = 2; break;
    case LFO_WAVEFORM_SAW_UP: current = 3; break;
    case LFO_WAVEFORM_SAW_DOWN: current = 4; break;
    case LFO_WAVEFORM_SAMPLE_HOLD: current = 5; break;
    case LFO_WAVEFORM_BIN: current = 6; break;
    case LFO_WAVEFORM_GLIDER: current = 7; break;
    case LFO_WAVEFORM_STRAY: current = 8; break;
    default: current = 0; break;
  }

  return menu_create_roller_page("Waveform",
    "Sine\nTriangle\nSquare\nSaw Up\nSaw Down\nS&&H\nBin\nGlider\nStray", current, waveform_confirm_cb, NULL);
}

static void nav_to_waveform(void* user_data) {
  (void)user_data;
  menu_navigate_to("Waveform", waveform_roller_create);
}

// ============================================================================
// Rate Mode Roller
// ============================================================================

static const lfo_rate_mode_t s_rate_modes[2] = {
  LFO_RATE_MODE_FREE,
  LFO_RATE_MODE_TEMPO,
};

static void rate_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index < 2) {
    scene->lfo1_config.rate_mode = s_rate_modes[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* rate_mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (scene->lfo1_config.rate_mode == LFO_RATE_MODE_TEMPO) ? 1 : 0;
  return menu_create_roller_page("Rate Mode", "Time\nDivision", current,
    rate_mode_confirm_cb, NULL);
}

static void nav_to_rate_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rate Mode", rate_mode_roller_create);
}

// ============================================================================
// Start Mode Roller
// ============================================================================

static void start_mode_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  lfo_start_mode_t modes[] = { LFO_START_RUNNING, LFO_START_PAUSED, LFO_START_TRANSPORT };
  if (selected_index < 3) {
    scene->lfo1_config.start_mode = modes[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* start_mode_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->lfo1_config.start_mode) {
    case LFO_START_RUNNING: current = 0; break;
    case LFO_START_PAUSED: current = 1; break;
    case LFO_START_TRANSPORT: current = 2; break;
  }
  return menu_create_roller_page("Start Mode", "Running\nPaused\nFollow Transport", 
    current, start_mode_confirm_cb, NULL);
}

static void nav_to_start_mode(void* user_data) {
  (void)user_data;
  menu_navigate_to("Start Mode", start_mode_roller_create);
}

// ============================================================================
// Trigger Timing Roller
// ============================================================================

static void trigger_timing_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  lfo_trigger_timing_t timings[] = { LFO_TRIGGER_IMMEDIATE, LFO_TRIGGER_NEXT_BEAT, LFO_TRIGGER_NEXT_BAR };
  if (selected_index < 3) {
    scene->lfo1_config.trigger_timing = timings[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* trigger_timing_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = 0;
  switch (scene->lfo1_config.trigger_timing) {
    case LFO_TRIGGER_IMMEDIATE: current = 0; break;
    case LFO_TRIGGER_NEXT_BEAT: current = 1; break;
    case LFO_TRIGGER_NEXT_BAR: current = 2; break;
  }
  return menu_create_roller_page("Trigger", "Immediate\nNext Beat\nNext Bar", 
    current, trigger_timing_confirm_cb, NULL);
}

static void nav_to_trigger_timing(void* user_data) {
  (void)user_data;
  menu_navigate_to("Trigger", trigger_timing_roller_create);
}

// ============================================================================
// Repeat Mode Roller
// ============================================================================

static void repeat_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->lfo1_config.repeat = (selected_index == 0);  // 0=Loop, 1=One-Shot
  lfo_apply_config(0, &scene->lfo1_config);
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* repeat_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = scene->lfo1_config.repeat ? 0 : 1;
  return menu_create_roller_page("Repeat", "Loop\nOne-Shot", 
    current, repeat_confirm_cb, NULL);
}

static void nav_to_repeat(void* user_data) {
  (void)user_data;
  menu_navigate_to("Repeat", repeat_roller_create);
}

// ============================================================================
// Phase Reset Toggle
// ============================================================================

static void phase_reset_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  scene->lfo1_config.reset_phase = (selected_index == 0);  // 0=Restart, 1=Continue
  lfo_apply_config(0, &scene->lfo1_config);
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* phase_reset_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = scene->lfo1_config.reset_phase ? 0 : 1;
  return menu_create_roller_page("On Restart", "From Start\nContinue", 
    current, phase_reset_confirm_cb, NULL);
}

static void nav_to_phase_reset(void* user_data) {
  (void)user_data;
  menu_navigate_to("On Restart", phase_reset_roller_create);
}

// ============================================================================
// Restore on Stop Roller
// ============================================================================

static void restore_on_stop_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  scene->lfo1_config.restore_on_stop = (selected_index == 1);  // 0=Nothing, 1=Restore
  lfo_apply_config(0, &scene->lfo1_config);
  persist_scene_changes();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* restore_on_stop_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = scene->lfo1_config.restore_on_stop ? 1 : 0;
  return menu_create_roller_page("On Stop", "Nothing\nRestore",
    current, restore_on_stop_confirm_cb, NULL);
}

static void nav_to_restore_on_stop(void* user_data) {
  (void)user_data;
  menu_navigate_to("On Stop", restore_on_stop_roller_create);
}

// ============================================================================
// Rate Hz Roller (for free mode)
// ============================================================================

static void rate_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  // Rate options: 0.1, 0.2, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 8.0, 10.0, 15.0, 20.0
  float rates[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 8.0f, 10.0f, 15.0f, 20.0f};
  if (selected_index < 12) {
    scene->lfo1_config.rate_hz_x100 = (uint16_t)(rates[selected_index] * 100.0f);
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* rate_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  float current_hz = scene->lfo1_config.rate_hz_x100 / 100.0f;
  float rates[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 8.0f, 10.0f, 15.0f, 20.0f};
  
  uint32_t current = 3;  // Default to 1.0 Hz
  for (int i = 0; i < 12; i++) {
    if (current_hz <= rates[i] + 0.01f) {
      current = i;
      break;
    }
  }
  
  return menu_create_roller_page("Rate", 
    "0.1 Hz\n0.2 Hz\n0.5 Hz\n1.0 Hz\n2.0 Hz\n3.0 Hz\n4.0 Hz\n5.0 Hz\n8.0 Hz\n10 Hz\n15 Hz\n20 Hz",
    current, rate_confirm_cb, NULL);
}

static void nav_to_rate(void* user_data) {
  (void)user_data;
  menu_navigate_to("Rate", rate_roller_create);
}

// ============================================================================
// Division Roller (for tempo mode)
// ============================================================================

static void division_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;
  
  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }
  
  if (selected_index < LFO_DIVISION_MAX) {
    scene->lfo1_config.division = (lfo_note_division_t)selected_index;
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* division_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  uint32_t current = (uint32_t)scene->lfo1_config.division;
  return menu_create_roller_page("Division", 
    "16 Bars\n12 Bars\n8 Bars\n4 Bars\n2 Bars\n1 Bar\n1/2 Note\n1/4 Note\n1/8 Note\n1/16 Note\n1/32 Note",
    current, division_confirm_cb, NULL);
}

static void nav_to_division(void* user_data) {
  (void)user_data;
  menu_navigate_to("Division", division_roller_create);
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
    scene->lfo1.cc_numbers[slot] = 0;
  } else if (selected_index < s_cc_options.count) {
    scene->lfo1.cc_numbers[slot] = s_cc_options.cc_numbers[selected_index];
  }
  
  // Recalculate num_cc_numbers
  scene->lfo1.num_cc_numbers = 0;
  for (int i = 0; i < 4; i++) {
    if (scene->lfo1.cc_numbers[i] > 0) {
      scene->lfo1.num_cc_numbers++;
    }
  }
  
  persist_scene_changes();
  
  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* cc_slot_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  
  if (!s_cc_options.options_str || s_cc_options.count == 0) {
    load_cc_options();
  }
  
  if (!s_cc_options.options_str) return NULL;
  
  uint8_t current_cc = scene->lfo1.cc_numbers[s_editing_cc_slot];
  uint32_t current_idx = (current_cc == 0) ? 0 : cc_number_to_option_index(current_cc);
  
  char title[16];
  snprintf(title, sizeof(title), "Parameter %d", s_editing_cc_slot + 1);
  
  return menu_create_roller_page(title, s_cc_options.options_str, current_idx, 
    cc_slot_confirm_cb, (void*)(uintptr_t)s_editing_cc_slot);
}

static void nav_to_cc_slot(void* user_data) {
  s_editing_cc_slot = (uint8_t)(uintptr_t)user_data;
  char title[16];
  snprintf(title, sizeof(title), "Parameter %d", s_editing_cc_slot + 1);
  menu_navigate_to(title, cc_slot_roller_create);
}

// ============================================================================
// Floor Roller (min_value)
// ============================================================================

static void floor_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index <= 127) {
    scene->lfo1_config.floor = (uint8_t)selected_index;
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* floor_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // Build options string for 0-127
  static char options[512];
  char* p = options;
  for (int i = 0; i <= 127; i++) {
    if (i > 0) *p++ = '\n';
    p += snprintf(p, options + sizeof(options) - p, "%d", i);
  }

  uint32_t current = scene->lfo1_config.floor;
  if (current > 127) current = 0;

  return menu_create_roller_page("Floor", options, current, floor_confirm_cb, NULL);
}

static void nav_to_floor(void* user_data) {
  (void)user_data;
  menu_navigate_to("Floor", floor_roller_create);
}

// ============================================================================
// Ceiling Roller (max_value)
// ============================================================================

static void ceiling_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  if (selected_index <= 127) {
    scene->lfo1_config.ceiling = (uint8_t)selected_index;
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* ceiling_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // Build options string for 0-127
  static char options[512];
  char* p = options;
  for (int i = 0; i <= 127; i++) {
    if (i > 0) *p++ = '\n';
    p += snprintf(p, options + sizeof(options) - p, "%d", i);
  }

  uint32_t current = scene->lfo1_config.ceiling;
  if (current > 127) current = 127;

  return menu_create_roller_page("Ceiling", options, current, ceiling_confirm_cb, NULL);
}

static void nav_to_ceiling(void* user_data) {
  (void)user_data;
  menu_navigate_to("Ceiling", ceiling_roller_create);
}

// ============================================================================
// Resolution Roller
// ============================================================================

static const char* resolution_to_display_string(lfo_resolution_mode_t mode) {
  switch (mode) {
    case LFO_RESOLUTION_AUTO: return "Auto";
    case LFO_RESOLUTION_COARSE: return "Coarse";
    case LFO_RESOLUTION_MEDIUM: return "Medium";
    case LFO_RESOLUTION_FINE: return "Fine";
    case LFO_RESOLUTION_MANUAL: return "Manual";
    default: return "Auto";
  }
}

static void resolution_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  lfo_resolution_mode_t modes[] = {
    LFO_RESOLUTION_AUTO, LFO_RESOLUTION_COARSE, LFO_RESOLUTION_MEDIUM,
    LFO_RESOLUTION_FINE, LFO_RESOLUTION_MANUAL
  };
  if (selected_index < 5) {
    scene->lfo1_config.resolution_mode = modes[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* resolution_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = (uint32_t)scene->lfo1_config.resolution_mode;
  if (current > 4) current = 0;

  return menu_create_roller_page("Resolution",
    "Auto\nCoarse\nMedium\nFine\nManual", current, resolution_confirm_cb, NULL);
}

static void nav_to_resolution(void* user_data) {
  (void)user_data;
  menu_navigate_to("Resolution", resolution_roller_create);
}

// ============================================================================
// Steps Roller (Manual mode only)
// ============================================================================

static void steps_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;

  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (!scene) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  uint8_t steps_values[] = {16, 32, 64, 128};
  if (selected_index < 4) {
    scene->lfo1_config.manual_steps = steps_values[selected_index];
    lfo_apply_config(0, &scene->lfo1_config);
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "LFO1", menu_page_lfo1_scene_create);
}

static lv_obj_t* steps_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  uint32_t current = 1;  // Default to 32
  uint8_t steps = scene->lfo1_config.manual_steps;
  if (steps <= 16) current = 0;
  else if (steps <= 32) current = 1;
  else if (steps <= 64) current = 2;
  else current = 3;

  return menu_create_roller_page("Steps", "16\n32\n64\n128", current, steps_confirm_cb, NULL);
}

static void nav_to_steps(void* user_data) {
  (void)user_data;
  menu_navigate_to("Steps", steps_roller_create);
}

// ============================================================================
// Main LFO1 Scene Page
// ============================================================================

lv_obj_t* menu_page_lfo1_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) {
    return menu_create_page_2line("LFO1", NULL, 0);
  }
  
  load_cc_options();
  
  int buf = get_next_buffer_set();
  int item_count = 0;
  
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  uint8_t mode_idx = lfo_get_current_mode_index(0, scene);
  snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "Mode\n%s",
    lfo_get_mode_name(0, mode_idx));
  s_lfo_items[item_count++] = (menu_item_t){
    s_mode_label[buf], nav_to_mode, NULL, true, MENU_ITEM_KIND_ROLLER
  };

  if (!scene->lfo1_config.enabled) {
    return menu_create_page_2line("LFO1", s_lfo_items, item_count);
  }
  
  // Start Mode
  snprintf(s_start_mode_label[buf], sizeof(s_start_mode_label[buf]), "Start Mode\n%s",
    start_mode_to_display_string(scene->lfo1_config.start_mode));
  s_lfo_items[item_count++] = (menu_item_t){s_start_mode_label[buf], nav_to_start_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Trigger Timing (only show if start_mode is paused - controlled by action)
  if (scene->lfo1_config.start_mode == LFO_START_PAUSED) {
    snprintf(s_trigger_timing_label[buf], sizeof(s_trigger_timing_label[buf]), "Trigger\n%s",
      trigger_timing_to_display_string(scene->lfo1_config.trigger_timing));
    s_lfo_items[item_count++] = (menu_item_t){s_trigger_timing_label[buf], nav_to_trigger_timing, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  
  // Repeat Mode
  snprintf(s_repeat_label[buf], sizeof(s_repeat_label[buf]), "Repeat\n%s",
    scene->lfo1_config.repeat ? "Loop" : "One-Shot");
  s_lfo_items[item_count++] = (menu_item_t){s_repeat_label[buf], nav_to_repeat, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Phase Reset
  snprintf(s_phase_reset_label[buf], sizeof(s_phase_reset_label[buf]), "On Restart\n%s",
    scene->lfo1_config.reset_phase ? "From Start" : "Continue");
  s_lfo_items[item_count++] = (menu_item_t){s_phase_reset_label[buf], nav_to_phase_reset, NULL, true, MENU_ITEM_KIND_ROLLER};

  // Restore on Stop
  snprintf(s_restore_on_stop_label[buf], sizeof(s_restore_on_stop_label[buf]), "On Stop\n%s",
    scene->lfo1_config.restore_on_stop ? "Restore" : "Nothing");
  s_lfo_items[item_count++] = (menu_item_t){s_restore_on_stop_label[buf], nav_to_restore_on_stop, NULL, true, MENU_ITEM_KIND_ROLLER};

  // Waveform
  snprintf(s_waveform_label[buf], sizeof(s_waveform_label[buf]), "Waveform\n%s",
    waveform_to_display_string(scene->lfo1_config.waveform));
  s_lfo_items[item_count++] = (menu_item_t){
    s_waveform_label[buf], nav_to_waveform, NULL, true, MENU_ITEM_KIND_ROLLER
  };
  
  // Rate Mode
  snprintf(s_rate_mode_label[buf], sizeof(s_rate_mode_label[buf]), "Rate Mode\n%s",
    rate_mode_to_display_string(scene->lfo1_config.rate_mode));
  s_lfo_items[item_count++] = (menu_item_t){s_rate_mode_label[buf], nav_to_rate_mode, NULL, true, MENU_ITEM_KIND_ROLLER};
  
  // Rate or Division based on mode (not shown for touchwheel mode)
  if (scene->lfo1_config.rate_mode == LFO_RATE_MODE_FREE) {
    float hz = scene->lfo1_config.rate_hz_x100 / 100.0f;
    snprintf(s_rate_label[buf], sizeof(s_rate_label[buf]), "Rate\n%.1f Hz", hz);
    s_lfo_items[item_count++] = (menu_item_t){s_rate_label[buf], nav_to_rate, NULL, true, MENU_ITEM_KIND_ROLLER};
  } else if (scene->lfo1_config.rate_mode == LFO_RATE_MODE_TEMPO) {
    snprintf(s_division_label[buf], sizeof(s_division_label[buf]), "Division\n%s",
      division_to_display_string(scene->lfo1_config.division));
    s_lfo_items[item_count++] = (menu_item_t){s_division_label[buf], nav_to_division, NULL, true, MENU_ITEM_KIND_ROLLER};
  }
  // For TOUCHWHEEL mode, no rate setting needed (controlled by touchwheel)
  
  if (scene_cv_claims_source(VELOCITY_MODE_LFO1)) {
    snprintf(s_mode_label[buf], sizeof(s_mode_label[buf]), "CV/Gate\nControlled");
    s_lfo_items[item_count++] = (menu_item_t){
      s_mode_label[buf], NULL, NULL, false, MENU_ITEM_KIND_DISPLAY
    };
    return menu_create_page_2line("LFO1", s_lfo_items, item_count);
  }

  if (scene->lfo1.output_type == OUTPUT_TYPE_CC) {
    for (int i = 0; i < 4; i++) {
      uint8_t cc_num = scene->lfo1.cc_numbers[i];
      if (cc_num > 0) {
        const char* cc_name = assets_get_cc_name(device, cc_num);
        if (cc_name && strcmp(cc_name, "Undefined") != 0) {
          snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
            "Parameter %d\n%s", i + 1, cc_name);
        } else {
          snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
            "Parameter %d\nCC %u", i + 1, (unsigned)cc_num);
        }
      } else {
        snprintf(s_cc_slot_labels[buf][i], sizeof(s_cc_slot_labels[buf][i]),
          "Parameter %d\nInactive", i + 1);
      }
      s_lfo_items[item_count++] = (menu_item_t){
        s_cc_slot_labels[buf][i], nav_to_cc_slot, (void*)(uintptr_t)i, true,
        MENU_ITEM_KIND_ROLLER
      };
    }
  }
  
  // Floor
  snprintf(s_floor_label[buf], sizeof(s_floor_label[buf]),
    "Floor\n%d", scene->lfo1_config.floor);
  s_lfo_items[item_count++] = (menu_item_t){s_floor_label[buf], nav_to_floor, NULL, true, MENU_ITEM_KIND_ROLLER};

  // Ceiling
  snprintf(s_ceiling_label[buf], sizeof(s_ceiling_label[buf]),
    "Ceiling\n%d", scene->lfo1_config.ceiling);
  s_lfo_items[item_count++] = (menu_item_t){s_ceiling_label[buf], nav_to_ceiling, NULL, true, MENU_ITEM_KIND_ROLLER};

  // Resolution (Glider streams continuously at 30 Hz — no step resolution)
  if (scene->lfo1_config.waveform != LFO_WAVEFORM_GLIDER) {
    snprintf(s_resolution_label[buf], sizeof(s_resolution_label[buf]),
      "Resolution\n%s", resolution_to_display_string(scene->lfo1_config.resolution_mode));
    s_lfo_items[item_count++] = (menu_item_t){s_resolution_label[buf], nav_to_resolution, NULL, true, MENU_ITEM_KIND_ROLLER};

    if (scene->lfo1_config.resolution_mode == LFO_RESOLUTION_MANUAL) {
      snprintf(s_steps_label[buf], sizeof(s_steps_label[buf]),
        "Steps\n%d", scene->lfo1_config.manual_steps);
      s_lfo_items[item_count++] = (menu_item_t){s_steps_label[buf], nav_to_steps, NULL, true, MENU_ITEM_KIND_ROLLER};
    }
  }

  return menu_create_page_2line("LFO1", s_lfo_items, item_count);
}

// Cleanup function
void menu_page_lfo1_scene_cleanup(void) {
  free_cc_options();
}
