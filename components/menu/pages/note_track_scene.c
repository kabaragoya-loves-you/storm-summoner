#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "curve.h"
#include "continuous_mapping.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

#define TAG "MENU_NOTE_TRACK_SCENE"

lv_obj_t* menu_page_note_track_scene_create(void);
void menu_page_note_track_scene_cleanup(void);

#define LABEL_BUFFER_SETS 2
static int s_current_buffer_set = 0;

#define MAX_NT_ITEMS 10
static menu_item_t s_nt_items[MAX_NT_ITEMS];

static char s_enabled_label[LABEL_BUFFER_SETS][32];
static char s_output_label[LABEL_BUFFER_SETS][32];
static char s_cc_label[LABEL_BUFFER_SETS][48];
static char s_polarity_label[LABEL_BUFFER_SETS][32];
static char s_curve_label[LABEL_BUFFER_SETS][32];
static char s_lfo_target_label[LABEL_BUFFER_SETS][32];

typedef struct {
  char* options_str;
  uint8_t* cc_numbers;
  uint16_t count;
} cc_options_t;

static cc_options_t s_cc_options = {0};
static bool s_callback_in_progress = false;

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

static void enabled_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene) {
    scene->note_track.enabled = (selected_index == 1);
    persist_scene_changes();
    ESP_LOGI(TAG, "Note Track per-scene %s", scene->note_track.enabled ? "enabled" : "disabled");
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* enabled_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  uint32_t cur = scene->note_track.enabled ? 1 : 0;
  return menu_create_roller_page("Enabled", "Disabled\nEnabled", cur, enabled_confirm_cb, NULL);
}

static void nav_to_enabled(void* user_data) {
  (void)user_data;
  menu_navigate_to("Enabled", enabled_roller_create);
}

static void output_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene) {
    output_type_t types[] = {
      OUTPUT_TYPE_CC,
      OUTPUT_TYPE_LFO_RATE, OUTPUT_TYPE_LFO_DEPTH,
      OUTPUT_TYPE_PITCH_BEND, OUTPUT_TYPE_TEMPO_NUDGE
    };
    if (selected_index < (sizeof(types) / sizeof(types[0]))) {
      scene->note_track.output_type = types[selected_index];
      persist_scene_changes();
    }
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* output_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  uint32_t cur = 0;
  switch (scene->note_track.output_type) {
    case OUTPUT_TYPE_CC: cur = 0; break;
    case OUTPUT_TYPE_LFO_RATE: cur = 1; break;
    case OUTPUT_TYPE_LFO_DEPTH: cur = 2; break;
    case OUTPUT_TYPE_PITCH_BEND: cur = 3; break;
    case OUTPUT_TYPE_TEMPO_NUDGE: cur = 4; break;
    default: cur = 0; break;
  }
  return menu_create_roller_page("Output",
    "Control Change\nLFO Rate\nLFO Depth\nPitch Bend\nTempo Nudge",
    cur, output_confirm_cb, NULL);
}

static void nav_to_output(void* user_data) {
  (void)user_data;
  menu_navigate_to("Output", output_roller_create);
}

static void cc_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene) {
    if (selected_index == 0) {
      scene->note_track.cc_numbers[0] = 0;
    } else if (selected_index < s_cc_options.count) {
      scene->note_track.cc_numbers[0] = s_cc_options.cc_numbers[selected_index];
    }
    scene->note_track.num_cc_numbers = (scene->note_track.cc_numbers[0] > 0) ? 1 : 0;
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* cc_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  if (!s_cc_options.options_str || s_cc_options.count == 0) load_cc_options();
  if (!s_cc_options.options_str) return NULL;

  uint8_t cc_num = scene->note_track.cc_numbers[0];
  uint32_t cur = (cc_num == 0) ? 0 : cc_number_to_option_index(cc_num);
  return menu_create_roller_page("CC", s_cc_options.options_str, cur, cc_confirm_cb, NULL);
}

static void nav_to_cc(void* user_data) {
  (void)user_data;
  menu_navigate_to("CC", cc_roller_create);
}

static void polarity_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene) {
    polarity_t pols[] = { POLARITY_UNIPOLAR, POLARITY_BIPOLAR, POLARITY_INVERTED };
    if (selected_index < 3) {
      scene->note_track.polarity = pols[selected_index];
      persist_scene_changes();
    }
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* polarity_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  uint32_t cur = 0;
  switch (scene->note_track.polarity) {
    case POLARITY_UNIPOLAR: cur = 0; break;
    case POLARITY_BIPOLAR: cur = 1; break;
    case POLARITY_INVERTED: cur = 2; break;
  }
  return menu_create_roller_page("Polarity", "Unipolar\nBipolar\nInverted", cur, polarity_confirm_cb, NULL);
}

static void nav_to_polarity(void* user_data) {
  (void)user_data;
  menu_navigate_to("Polarity", polarity_roller_create);
}

static void curve_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene) {
    curve_type_t curves[] = { CURVE_LINEAR, CURVE_EXPONENTIAL, CURVE_LOGARITHMIC, CURVE_S_CURVE };
    if (selected_index < 4) {
      scene->note_track.curve.type = curves[selected_index];
      persist_scene_changes();
    }
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* curve_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  uint32_t cur = 0;
  switch (scene->note_track.curve.type) {
    case CURVE_LINEAR: cur = 0; break;
    case CURVE_EXPONENTIAL: cur = 1; break;
    case CURVE_LOGARITHMIC: cur = 2; break;
    case CURVE_S_CURVE: cur = 3; break;
    default: cur = 0; break;
  }
  return menu_create_roller_page("Curve", "Linear\nExponential\nLogarithmic\nS-Curve", cur, curve_confirm_cb, NULL);
}

static void nav_to_curve(void* user_data) {
  (void)user_data;
  menu_navigate_to("Curve", curve_roller_create);
}

static void lfo_target_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  scene_t* scene = scene_get_current();
  if (scene && selected_index < 3) {
    scene->note_track.lfo_target = (lfo_target_t)selected_index;
    persist_scene_changes();
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Note Track", menu_page_note_track_scene_create);
}

static lv_obj_t* lfo_target_roller_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return NULL;
  uint32_t cur = (uint32_t)scene->note_track.lfo_target;
  return menu_create_roller_page("LFO Target", "LFO1\nLFO2\nBoth", cur, lfo_target_confirm_cb, NULL);
}

static void nav_to_lfo_target(void* user_data) {
  (void)user_data;
  menu_navigate_to("LFO Target", lfo_target_roller_create);
}

lv_obj_t* menu_page_note_track_scene_create(void) {
  scene_t* scene = scene_get_current();
  if (!scene) return menu_create_page_2line("Note Track", NULL, 0);

  int buf = get_next_buffer_set();
  int n = 0;
  bool enabled = scene->note_track.enabled;

  snprintf(s_enabled_label[buf], sizeof(s_enabled_label[buf]),
    "Enabled\n%s", enabled ? "Yes" : "No");
  s_nt_items[n++] = (menu_item_t){ s_enabled_label[buf], nav_to_enabled, NULL, true };

  if (enabled) {
    load_cc_options();

    const char* output_name;
    switch (scene->note_track.output_type) {
      case OUTPUT_TYPE_CC: output_name = "Control Change"; break;
      case OUTPUT_TYPE_LFO_RATE: output_name = "LFO Rate"; break;
      case OUTPUT_TYPE_LFO_DEPTH: output_name = "LFO Depth"; break;
      case OUTPUT_TYPE_PITCH_BEND: output_name = "Pitch Bend"; break;
      case OUTPUT_TYPE_TEMPO_NUDGE: output_name = "Tempo Nudge"; break;
      default: output_name = "Control Change"; break;
    }
    snprintf(s_output_label[buf], sizeof(s_output_label[buf]), "Output\n%s", output_name);
    s_nt_items[n++] = (menu_item_t){ s_output_label[buf], nav_to_output, NULL, true };

    if (scene->note_track.output_type == OUTPUT_TYPE_CC) {
      uint8_t cc_num = scene->note_track.cc_numbers[0];
      if (cc_num > 0) {
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_get_current_index());
        const char* cc_name = assets_get_cc_name(device, cc_num);
        if (cc_name && strcmp(cc_name, "Undefined") != 0)
          snprintf(s_cc_label[buf], sizeof(s_cc_label[buf]), "CC\n%s", cc_name);
        else
          snprintf(s_cc_label[buf], sizeof(s_cc_label[buf]), "CC\nCC %u", (unsigned)cc_num);
      } else {
        snprintf(s_cc_label[buf], sizeof(s_cc_label[buf]), "CC\nInactive");
      }
      s_nt_items[n++] = (menu_item_t){ s_cc_label[buf], nav_to_cc, NULL, true };
    }

    if (scene->note_track.output_type == OUTPUT_TYPE_LFO_RATE ||
        scene->note_track.output_type == OUTPUT_TYPE_LFO_DEPTH) {
      snprintf(s_lfo_target_label[buf], sizeof(s_lfo_target_label[buf]),
        "LFO Target\n%s", lfo_target_to_string(scene->note_track.lfo_target));
      s_nt_items[n++] = (menu_item_t){ s_lfo_target_label[buf], nav_to_lfo_target, NULL, true };
    }

    snprintf(s_polarity_label[buf], sizeof(s_polarity_label[buf]),
      "Polarity\n%s", polarity_to_string(scene->note_track.polarity));
    s_nt_items[n++] = (menu_item_t){ s_polarity_label[buf], nav_to_polarity, NULL, true };

    snprintf(s_curve_label[buf], sizeof(s_curve_label[buf]),
      "Curve\n%s", curve_type_to_string(scene->note_track.curve.type));
    s_nt_items[n++] = (menu_item_t){ s_curve_label[buf], nav_to_curve, NULL, true };
  }

  return menu_create_page_2line("Note Track", s_nt_items, n);
}

void menu_page_note_track_scene_cleanup(void) {
  free_cc_options();
}
