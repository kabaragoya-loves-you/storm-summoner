#include "continuous_flag_rollers.h"
#include "menu_pages.h"
#include <stdio.h>
#include <string.h>

static continuous_flag_ctx_t s_ctx = {0};
static bool s_callback_in_progress = false;
static continuous_flag_threshold_kind_t s_editing_kind = CONTINUOUS_FLAG_RAISE_ABOVE;

static char s_threshold_options[512];

void continuous_flag_ctx_set(const continuous_flag_ctx_t* ctx) {
  if (ctx) s_ctx = *ctx;
}

static continuous_mapping_t* current_mapping(void) {
  scene_t* scene = scene_get_current();
  if (!scene || !s_ctx.get_mapping) return NULL;
  return s_ctx.get_mapping(scene);
}

static const char* threshold_title(continuous_flag_threshold_kind_t kind) {
  switch (kind) {
    case CONTINUOUS_FLAG_RAISE_ABOVE: return "Raise Flag Above";
    case CONTINUOUS_FLAG_RAISE_BELOW: return "Raise Flag Below";
    case CONTINUOUS_FLAG_LOWER_ABOVE: return "Lower Flag Above";
    case CONTINUOUS_FLAG_LOWER_BELOW: return "Lower Flag Below";
    default: return "Flag Threshold";
  }
}

static uint8_t threshold_value(const continuous_mapping_t* m,
    continuous_flag_threshold_kind_t kind) {
  if (!m) return CONTINUOUS_FLAG_THRESHOLD_OFF;
  switch (kind) {
    case CONTINUOUS_FLAG_RAISE_ABOVE: return m->flag_raise_above;
    case CONTINUOUS_FLAG_RAISE_BELOW: return m->flag_raise_below;
    case CONTINUOUS_FLAG_LOWER_ABOVE: return m->flag_lower_above;
    case CONTINUOUS_FLAG_LOWER_BELOW: return m->flag_lower_below;
    default: return CONTINUOUS_FLAG_THRESHOLD_OFF;
  }
}

static uint8_t* threshold_field(continuous_mapping_t* m,
    continuous_flag_threshold_kind_t kind) {
  if (!m) return NULL;
  switch (kind) {
    case CONTINUOUS_FLAG_RAISE_ABOVE: return &m->flag_raise_above;
    case CONTINUOUS_FLAG_RAISE_BELOW: return &m->flag_raise_below;
    case CONTINUOUS_FLAG_LOWER_ABOVE: return &m->flag_lower_above;
    case CONTINUOUS_FLAG_LOWER_BELOW: return &m->flag_lower_below;
    default: return NULL;
  }
}

static bool kind_is_above(continuous_flag_threshold_kind_t kind) {
  return kind == CONTINUOUS_FLAG_RAISE_ABOVE ||
    kind == CONTINUOUS_FLAG_LOWER_ABOVE;
}

// Show the row if it is already set, or if at least one numeric value is legal.
static bool threshold_row_visible(const continuous_mapping_t* m,
    continuous_flag_threshold_kind_t kind) {
  if (threshold_value(m, kind) != CONTINUOUS_FLAG_THRESHOLD_OFF)
    return true;
  uint8_t min_val = kind_is_above(kind) ? 0 : 1;
  uint8_t max_val = kind_is_above(kind) ? 126 : 127;
  for (uint8_t v = min_val; v <= max_val; v++) {
    if (continuous_mapping_flag_threshold_allowed(m, kind, v))
      return true;
  }
  return false;
}

static void build_threshold_options(continuous_flag_threshold_kind_t kind,
    const continuous_mapping_t* m) {
  char* p = s_threshold_options;
  char* end = s_threshold_options + sizeof(s_threshold_options);
  int written = snprintf(p, end - p, "Off");
  if (written <= 0 || written >= end - p) return;
  p += written;

  uint8_t min_val = kind_is_above(kind) ? 0 : 1;
  uint8_t max_val = kind_is_above(kind) ? 126 : 127;
  for (uint8_t v = min_val; v <= max_val; v++) {
    if (!continuous_mapping_flag_threshold_allowed(m, kind, v)) continue;
    written = snprintf(p, end - p, "\n%u", (unsigned)v);
    if (written <= 0 || written >= end - p) break;
    p += written;
  }
}

static uint32_t threshold_current_index(continuous_flag_threshold_kind_t kind,
    const continuous_mapping_t* m) {
  uint8_t value = threshold_value(m, kind);
  if (value == CONTINUOUS_FLAG_THRESHOLD_OFF) return 0;

  uint32_t idx = 0;
  uint8_t min_val = kind_is_above(kind) ? 0 : 1;
  uint8_t max_val = kind_is_above(kind) ? 126 : 127;
  for (uint8_t v = min_val; v <= max_val; v++) {
    if (!continuous_mapping_flag_threshold_allowed(m, kind, v)) continue;
    idx++;
    if (v == value) return idx;
  }
  return 0;
}

static uint8_t value_from_index(continuous_flag_threshold_kind_t kind,
    const continuous_mapping_t* m, uint32_t selected_index) {
  if (selected_index == 0) return CONTINUOUS_FLAG_THRESHOLD_OFF;

  uint32_t idx = 0;
  uint8_t min_val = kind_is_above(kind) ? 0 : 1;
  uint8_t max_val = kind_is_above(kind) ? 126 : 127;
  for (uint8_t v = min_val; v <= max_val; v++) {
    if (!continuous_mapping_flag_threshold_allowed(m, kind, v)) continue;
    idx++;
    if (idx == selected_index) return v;
  }
  return CONTINUOUS_FLAG_THRESHOLD_OFF;
}

static void threshold_confirm_cb(uint32_t selected_index, void* user_data) {
  (void)user_data;
  if (s_callback_in_progress) return;
  s_callback_in_progress = true;

  continuous_mapping_t* m = current_mapping();
  uint8_t* field = threshold_field(m, s_editing_kind);
  if (!m || !field || !s_ctx.persist || !s_ctx.parent_title || !s_ctx.parent_create) {
    s_callback_in_progress = false;
    menu_navigate_back();
    return;
  }

  uint8_t value = value_from_index(s_editing_kind, m, selected_index);
  if (value == CONTINUOUS_FLAG_THRESHOLD_OFF ||
      continuous_mapping_flag_threshold_allowed(m, s_editing_kind, value))
    *field = value;

  continuous_mapping_sanitize_flag_thresholds(m);
  s_ctx.persist();

  s_callback_in_progress = false;
  menu_navigate_back_then_to(2, s_ctx.parent_title, s_ctx.parent_create);
}

static lv_obj_t* threshold_roller_create(void) {
  continuous_mapping_t* m = current_mapping();
  if (!m) return NULL;

  build_threshold_options(s_editing_kind, m);
  return menu_create_roller_page(threshold_title(s_editing_kind),
    s_threshold_options, threshold_current_index(s_editing_kind, m),
    threshold_confirm_cb, NULL);
}

static void nav_to_threshold(void* user_data) {
  s_editing_kind = (continuous_flag_threshold_kind_t)(uintptr_t)user_data;
  menu_navigate_to(threshold_title(s_editing_kind), threshold_roller_create);
}

static void format_threshold_label(char* buf, size_t buf_size,
    const char* title, uint8_t value) {
  if (value == CONTINUOUS_FLAG_THRESHOLD_OFF)
    snprintf(buf, buf_size, "%s\nOff", title);
  else
    snprintf(buf, buf_size, "%s\n%u", title, (unsigned)value);
}

int continuous_flag_append_cc_items(menu_item_t* items, char labels[4][48], int idx) {
  if (!items || !labels) return idx;

  continuous_mapping_t* m = current_mapping();
  if (!m) return idx;

  static const continuous_flag_threshold_kind_t kinds[4] = {
    CONTINUOUS_FLAG_RAISE_ABOVE,
    CONTINUOUS_FLAG_RAISE_BELOW,
    CONTINUOUS_FLAG_LOWER_ABOVE,
    CONTINUOUS_FLAG_LOWER_BELOW,
  };
  static const char* titles[4] = {
    "Raise Flag Above",
    "Raise Flag Below",
    "Lower Flag Above",
    "Lower Flag Below",
  };

  for (int i = 0; i < 4; i++) {
    if (!threshold_row_visible(m, kinds[i])) continue;
    format_threshold_label(labels[i], 48, titles[i], threshold_value(m, kinds[i]));
    items[idx++] = (menu_item_t){
      labels[i], nav_to_threshold, (void*)(uintptr_t)kinds[i],
      true, MENU_ITEM_KIND_ROLLER
    };
  }

  return idx;
}
