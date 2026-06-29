#include "menu.h"
#include "menu_pages.h"
#include "scene.h"
#include "param_stream.h"
#include "assets_manager.h"
#include "assets_types.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Static Storage
// ============================================================================

static char s_channel_labels[SCOPE_CHANNEL_COUNT][48];
static uint8_t s_editing_channel = 0;
static bool s_callback_in_progress = false;

// Combined source option list: "Off" + named param targets + device CCs.
// Parallel arrays map each roller option index to a (kind, id) pair.
typedef struct {
  char* options_str;   // newline-separated labels
  uint8_t* kinds;      // SCOPE_SRC_* per option
  uint8_t* ids;        // param_target_t value or CC number per option
  uint16_t count;
} scope_options_t;

static scope_options_t s_opts = {0};

// ============================================================================
// Source option list
// ============================================================================

static void free_options(void) {
  if (s_opts.options_str) { heap_caps_free(s_opts.options_str); s_opts.options_str = NULL; }
  if (s_opts.kinds) { heap_caps_free(s_opts.kinds); s_opts.kinds = NULL; }
  if (s_opts.ids) { heap_caps_free(s_opts.ids); s_opts.ids = NULL; }
  s_opts.count = 0;
}

static bool build_options(void) {
  free_options();

  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

  uint16_t cc_count = 0;
  if (device) {
    for (uint16_t i = 0; i < device->control_count; i++) {
      if (device->controls[i].type == MIDI_CONTROL_TYPE_CC) cc_count++;
    }
  }

  uint16_t total = 1 + PARAM_TARGET_COUNT + cc_count;  // "Off" + named + CCs
  s_opts.kinds = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  s_opts.ids = heap_caps_calloc(total, sizeof(uint8_t), MALLOC_CAP_SPIRAM);
  size_t str_size = total * 28;
  s_opts.options_str = heap_caps_calloc(str_size, 1, MALLOC_CAP_SPIRAM);

  if (!s_opts.kinds || !s_opts.ids || !s_opts.options_str) {
    free_options();
    return false;
  }

  size_t pos = 0;
  uint16_t n = 0;

  // Index 0: Off
  pos += snprintf(s_opts.options_str + pos, str_size - pos, "Off");
  s_opts.kinds[n] = SCOPE_SRC_NONE;
  s_opts.ids[n] = 0;
  n++;

  // Named param targets
  for (int t = 0; t < PARAM_TARGET_COUNT && pos + 28 < str_size; t++) {
    pos += snprintf(s_opts.options_str + pos, str_size - pos, "\n%s",
      param_target_display_name((param_target_t)t));
    s_opts.kinds[n] = SCOPE_SRC_PARAM;
    s_opts.ids[n] = (uint8_t)t;
    n++;
  }

  // Device CCs
  if (device) {
    for (uint16_t i = 0; i < device->control_count && pos + 28 < str_size; i++) {
      const midi_control_t* ctrl = &device->controls[i];
      if (ctrl->type != MIDI_CONTROL_TYPE_CC) continue;
      const char* name = ctrl->name ? ctrl->name : "Unknown";
      pos += snprintf(s_opts.options_str + pos, str_size - pos, "\n%.24s", name);
      s_opts.kinds[n] = SCOPE_SRC_CC;
      s_opts.ids[n] = (uint8_t)ctrl->id;
      n++;
    }
  }

  s_opts.count = n;
  return true;
}

// Find the option index matching a channel's current (kind, id)
static uint32_t option_index_for_channel(const scope_channel_t* chn) {
  if (!chn || chn->kind == SCOPE_SRC_NONE) return 0;
  for (uint16_t i = 1; i < s_opts.count; i++) {
    if (s_opts.kinds[i] == chn->kind && s_opts.ids[i] == chn->id) return i;
  }
  return 0;
}

// Human-readable name for a channel's current source (for the list label)
static void channel_source_name(const scope_channel_t* chn, char* buf, size_t buf_size) {
  if (!chn || chn->kind == SCOPE_SRC_NONE) {
    snprintf(buf, buf_size, "Off");
    return;
  }
  if (chn->kind == SCOPE_SRC_PARAM) {
    snprintf(buf, buf_size, "%s", param_target_display_name((param_target_t)chn->id));
    return;
  }
  // CC: prefer the device's control name
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  const char* cc_name = device ? assets_get_cc_name(device, chn->id) : NULL;
  if (cc_name && strcmp(cc_name, "Undefined") != 0) {
    snprintf(buf, buf_size, "%.30s", cc_name);
  } else {
    snprintf(buf, buf_size, "CC %u", (unsigned)chn->id);
  }
}

// ============================================================================
// Channel Source Roller
// ============================================================================

static void channel_confirm_cb(uint32_t selected_index, void* user_data) {
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  uint8_t channel = (uint8_t)(uintptr_t)user_data;

  if (selected_index < s_opts.count) {
    scene_set_scope_channel(scene_get_current_index(), channel,
      s_opts.kinds[selected_index], s_opts.ids[selected_index]);
  }

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, "Scope", menu_page_scope_config_create);
}

static lv_obj_t* channel_roller_create(void) {
  if (!build_options() || !s_opts.options_str) return NULL;

  const scope_channel_t* chn =
    scene_get_scope_channel(scene_get_current_index(), s_editing_channel);
  uint32_t current_idx = option_index_for_channel(chn);

  char title[16];
  snprintf(title, sizeof(title), "Channel %u", (unsigned)(s_editing_channel + 1));

  return menu_create_roller_page(title, s_opts.options_str, current_idx,
    channel_confirm_cb, (void*)(uintptr_t)s_editing_channel);
}

static void nav_to_channel(void* user_data) {
  s_editing_channel = (uint8_t)(uintptr_t)user_data;
  char title[16];
  snprintf(title, sizeof(title), "Channel %u", (unsigned)(s_editing_channel + 1));
  menu_navigate_to(title, channel_roller_create);
}

// ============================================================================
// Main Scope Config Page
// ============================================================================

lv_obj_t* menu_page_scope_config_create(void) {
  static menu_item_t items[SCOPE_CHANNEL_COUNT];
  int item_count = 0;

  uint8_t scene_index = scene_get_current_index();
  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    const scope_channel_t* chn = scene_get_scope_channel(scene_index, i);
    char src_name[32];
    channel_source_name(chn, src_name, sizeof(src_name));
    snprintf(s_channel_labels[i], sizeof(s_channel_labels[i]),
      "Channel %d: %s", i + 1, src_name);
    items[item_count++] = (menu_item_t){
      s_channel_labels[i], nav_to_channel, (void*)(uintptr_t)i, true,
      MENU_ITEM_KIND_ROLLER
    };
  }

  return menu_create_page("Scope", items, item_count);
}

// Cleanup function
void menu_page_scope_config_cleanup(void) {
  free_options();
}
