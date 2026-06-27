// Scene -> CC Defaults
//
// Per-scene default values for the current device's CCs. A CC with a default is
// transmitted on scene load and seeds the live CC cache that drives x_variants /
// x_noop resolution. Non-mandatory CCs may be set to "None" (no default).
// Mandatory (gate/mode) CCs are required and always carry a value.

#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "assets_types.h"
#include "assets_manager.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CCD_ITEMS 130

static menu_item_t s_ccd_items[MAX_CCD_ITEMS];
static char s_ccd_labels[MAX_CCD_ITEMS][40];
static char s_ccd_title[24];

// Pending state for the per-CC value roller (zero-arg builder reads these).
static uint8_t s_ccd_cc;
static char s_ccd_options[1024];
static uint32_t s_ccd_initial_index;
static uint8_t s_ccd_index_values[MAX_CCD_ITEMS];  // roller index -> value (0xFF = None)
static uint16_t s_ccd_index_count;
static char s_ccd_value_title[32];

static const device_def_t* ccd_device(void) {
  return (const device_def_t*)scene_get_device(scene_get_current_index());
}

static const midi_control_t* ccd_effective(const device_def_t* device, uint8_t cc) {
  static midi_control_t scratch;
  return assets_get_effective_control(device, cc, NULL, &scratch);
}

static void ccd_add_option(const char* label, uint8_t value, uint8_t current) {
  if (s_ccd_index_count >= MAX_CCD_ITEMS) return;
  if (s_ccd_index_count > 0)
    strncat(s_ccd_options, "\n", sizeof(s_ccd_options) - strlen(s_ccd_options) - 1);
  char trunc[28];
  strncpy(trunc, label, sizeof(trunc) - 1);
  trunc[sizeof(trunc) - 1] = '\0';
  strncat(s_ccd_options, trunc, sizeof(s_ccd_options) - strlen(s_ccd_options) - 1);
  if (value == current) s_ccd_initial_index = s_ccd_index_count;
  s_ccd_index_values[s_ccd_index_count++] = value;
}

static void ccd_value_confirm_cb(uint32_t selected_index, void* user_data) {
  uint8_t cc = (uint8_t)(uintptr_t)user_data;
  if (selected_index < s_ccd_index_count) {
    scene_set_cc_default(scene_get_current_index(), cc, s_ccd_index_values[selected_index]);
  }
  // Pop the value roller and rebuild the list so the new value shows.
  menu_pop_then_replace_deferred(1, "CC Defaults", menu_page_cc_defaults_scene_create);
}

static lv_obj_t* ccd_value_roller_create(void) {
  const device_def_t* device = ccd_device();
  uint8_t cc = s_ccd_cc;
  uint8_t scene_index = scene_get_current_index();
  const midi_control_t* eff = ccd_effective(device, cc);
  bool mandatory = assets_cc_is_mandatory(device, cc);
  uint8_t current = scene_get_cc_default(scene_index, cc);

  s_ccd_options[0] = '\0';
  s_ccd_index_count = 0;
  s_ccd_initial_index = 0;

  // Non-mandatory CCs can be cleared back to "no default".
  if (!mandatory) ccd_add_option("None", SCENE_CC_DEFAULT_NONE, current);

  if (eff && eff->discrete_count > 0) {
    for (int i = 0; i < eff->discrete_count; i++) {
      const char* name = eff->discrete_values[i].name;
      uint8_t val = (uint8_t)eff->discrete_values[i].value;
      if (name) {
        ccd_add_option(name, val, current);
      } else {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)val);
        ccd_add_option(num, val, current);
      }
    }
  } else {
    uint16_t min_val = eff ? eff->min : 0;
    uint16_t max_val = eff ? eff->max : 127;
    if (max_val > 127) max_val = 127;
    for (uint16_t v = min_val; v <= max_val; v++) {
      char num[8];
      snprintf(num, sizeof(num), "%u", (unsigned)v);
      ccd_add_option(num, (uint8_t)v, current);
    }
  }

  const char* name = (eff && eff->name) ? eff->name : NULL;
  if (name)
    snprintf(s_ccd_value_title, sizeof(s_ccd_value_title), "%.20s", name);
  else
    snprintf(s_ccd_value_title, sizeof(s_ccd_value_title), "CC%u", (unsigned)cc);

  return menu_create_roller_page(s_ccd_value_title, s_ccd_options, s_ccd_initial_index,
    ccd_value_confirm_cb, (void*)(uintptr_t)cc);
}

static void ccd_open_value(void* user_data) {
  s_ccd_cc = (uint8_t)(uintptr_t)user_data;
  menu_navigate_to("Value", ccd_value_roller_create);
}

// Append one CC row to the menu at *idx (bounded by MAX_CCD_ITEMS).
static void ccd_add_row(const device_def_t* device, uint8_t scene_index,
  uint8_t cc, int* idx) {
  if (*idx >= MAX_CCD_ITEMS) return;
  const midi_control_t* eff = ccd_effective(device, cc);
  const midi_control_t* base = assets_get_control_by_cc(device, cc);
  const char* name = (eff && eff->name) ? eff->name
    : (base && base->name ? base->name : "CC");
  uint8_t cur = scene_get_cc_default(scene_index, cc);

  char valbuf[16];
  if (cur == SCENE_CC_DEFAULT_NONE) {
    snprintf(valbuf, sizeof(valbuf), "None");
  } else if (eff && eff->discrete_count > 0) {
    const char* dn = NULL;
    for (int d = 0; d < eff->discrete_count; d++) {
      if (eff->discrete_values[d].value == cur) { dn = eff->discrete_values[d].name; break; }
    }
    if (dn) snprintf(valbuf, sizeof(valbuf), "%.14s", dn);
    else snprintf(valbuf, sizeof(valbuf), "%u", (unsigned)cur);
  } else {
    snprintf(valbuf, sizeof(valbuf), "%u", (unsigned)cur);
  }

  snprintf(s_ccd_labels[*idx], sizeof(s_ccd_labels[*idx]), "%.14s: %s", name, valbuf);
  s_ccd_items[*idx] = (menu_item_t){
    s_ccd_labels[*idx], ccd_open_value, (void*)(uintptr_t)cc, true, MENU_ITEM_KIND_ROLLER
  };
  (*idx)++;
}

lv_obj_t* menu_page_cc_defaults_scene_create(void) {
  const device_def_t* device = ccd_device();
  uint8_t scene_index = scene_get_current_index();
  int idx = 0;

  snprintf(s_ccd_title, sizeof(s_ccd_title), "CC Defaults");

  // Make sure required gate CCs already show their seeded value.
  scene_ensure_mandatory_cc_defaults();

  if (device && device->controls) {
    // Mandatory (gate) CCs first, so the mode selector(s) sit at the top.
    int mandatory_count = 0;
    for (uint16_t i = 0; i < device->control_count && idx < MAX_CCD_ITEMS; i++) {
      const midi_control_t* ctrl = &device->controls[i];
      if (ctrl->type != MIDI_CONTROL_TYPE_CC) continue;
      uint8_t cc = (uint8_t)ctrl->id;
      if (!assets_cc_is_mandatory(device, cc)) continue;
      ccd_add_row(device, scene_index, cc, &idx);
      mandatory_count++;
    }

    // Divider below the last mandatory parameter (only when there is one).
    if (mandatory_count > 0 && idx < MAX_CCD_ITEMS) {
      s_ccd_items[idx++] = (menu_item_t){ "---", NULL, NULL, false, MENU_ITEM_KIND_DISPLAY };
    }

    // Remaining CCs, hiding no-ops in the current mode.
    for (uint16_t i = 0; i < device->control_count && idx < MAX_CCD_ITEMS; i++) {
      const midi_control_t* ctrl = &device->controls[i];
      if (ctrl->type != MIDI_CONTROL_TYPE_CC) continue;
      uint8_t cc = (uint8_t)ctrl->id;
      if (assets_cc_is_mandatory(device, cc)) continue;
      if (assets_cc_is_noop(device, cc)) continue;
      ccd_add_row(device, scene_index, cc, &idx);
    }
  }

  if (idx == 0) {
    s_ccd_items[idx++] = (menu_item_t){
      "No CC parameters", NULL, NULL, false, MENU_ITEM_KIND_DISPLAY
    };
  }

  return menu_create_page(s_ccd_title, s_ccd_items, idx);
}
