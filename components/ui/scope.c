#include "lvgl.h"
#include "ui.h"
#include "scene.h"
#include "param_stream.h"
#include "action.h"
#include "assets_manager.h"
#include "event_bus.h"
#include "tempo.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "SCOPE"

//=============================================================================
// CONFIGURATION
//=============================================================================

#define SCOPE_SAMPLE_MS 33  // fallback before tempo is available

#define SCOPE_BOX_FRACTION 0.58f

// Half as many sample columns as pixels (resolution); scroll rate is tempo-synced.
#define SCOPE_HISTORY_DIVISOR 2

#define SCOPE_CHART_BOTTOM_GAP 3
#define SCOPE_SCENE_BOTTOM_OFFSET 25
#define SCOPE_CHART_TOP_GAP 2

#define SCOPE_LEGEND_PANEL_X -5
#define SCOPE_LEGEND_PANEL_Y 9
#define SCOPE_LEGEND_ROW_H 16
#define SCOPE_LEGEND_COL_GAP 14
#define SCOPE_LEGEND_PANEL_PAD 4
#define SCOPE_SWATCH_W 10
#define SCOPE_LINE_WIDTH 2
#define SCOPE_VALUE_X_OFFSET 3

static const uint32_t scope_colors[SCOPE_CHANNEL_COUNT] = {
  0x00E5FF,  // cyan
  0xFF00FF,  // magenta
  0xFFEA00,  // yellow
  0x00E676,  // green
};

LV_FONT_DECLARE(chalet_ny_14);
LV_FONT_DECLARE(flyer_venice_20);

//=============================================================================
// STATE
//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_plot_area = NULL;
static lv_obj_t *g_chart = NULL;
static lv_chart_series_t *g_series[SCOPE_CHANNEL_COUNT] = {0};
static lv_timer_t *g_sample_timer = NULL;

static lv_obj_t *g_legend_panel = NULL;
static lv_obj_t *g_legend_row[2] = {NULL, NULL};
static lv_obj_t *g_legend_rows[SCOPE_CHANNEL_COUNT] = {0};
static lv_obj_t *g_legend_swatches[SCOPE_CHANNEL_COUNT] = {0};
static lv_obj_t *g_legend_labels[SCOPE_CHANNEL_COUNT] = {0};
static lv_obj_t *g_value_labels[SCOPE_CHANNEL_COUNT] = {0};
static lv_obj_t *g_scene_label = NULL;

static char g_scene_info_text[48];
static char g_value_text[SCOPE_CHANNEL_COUNT][8];
static uint8_t g_last_value_shown[SCOPE_CHANNEL_COUNT];

static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;
static uint16_t g_chart_width = 0;
static uint16_t g_chart_height = 0;
static int16_t g_chart_top = 0;
static uint16_t g_point_count = 0;
static int16_t g_value_font_h = 0;

static volatile uint16_t g_bpm_x10 = TEMPO_DEFAULT_BPM_X10;
static volatile bool g_tempo_timing_dirty = false;

static volatile bool g_module_active = false;
static volatile bool g_config_dirty = false;
static volatile bool g_scene_info_dirty = false;

static uint8_t g_cached_kind[SCOPE_CHANNEL_COUNT];
static uint8_t g_cached_id[SCOPE_CHANNEL_COUNT];
static bool g_channel_cache_valid = false;

//=============================================================================
// FORWARD DECLARATIONS
//=============================================================================

static void scope_apply_config(void);
static void scope_update_layout(void);
static void scope_update_legend(void);
static void scope_sync_channel_display(uint8_t scene_index);
static void scope_update_value_labels(const scene_t *scene, uint8_t scene_index,
  const uint8_t *values);
static void scope_update_scene_label(void);
static void scope_update_sample_period(void);
static void scope_sample_cb(lv_timer_t *timer);
static void scope_scene_changed_handler(const event_t *event, void *context);
static void scope_tempo_handler(const event_t *event, void *context);

//=============================================================================
// HELPERS
//=============================================================================

static uint8_t scope_get_param_value(const scene_t *scene, param_target_t target) {
  return param_target_scope_value(scene, target);
}

static uint8_t scope_get_channel_value(const scene_t *scene, const scope_channel_t *chn) {
  if (!scene || !chn || chn->kind == SCOPE_SRC_NONE) return 0;
  if (chn->kind == SCOPE_SRC_CC) return action_get_cc_value(chn->id);
  return scope_get_param_value(scene, (param_target_t)chn->id);
}

static void scope_channel_label(const scope_channel_t *chn, char *buf, size_t buf_size) {
  if (!chn || chn->kind == SCOPE_SRC_NONE) {
    snprintf(buf, buf_size, "Off");
    return;
  }
  if (chn->kind == SCOPE_SRC_PARAM) {
    snprintf(buf, buf_size, "%s", param_target_display_name((param_target_t)chn->id));
    return;
  }
  const device_def_t *device =
    (const device_def_t *)scene_get_device(scene_get_current_index());
  const char *cc_name = device ? assets_get_cc_name(device, chn->id) : NULL;
  if (cc_name && strcmp(cc_name, "Undefined") != 0)
    snprintf(buf, buf_size, "%.10s", cc_name);
  else
    snprintf(buf, buf_size, "CC %u", (unsigned)chn->id);
}

static int16_t scope_value_to_line_y(uint8_t value) {
  if (g_chart_height <= 1) return g_chart_top;
  return (int16_t)g_chart_top + (int16_t)g_chart_height - 1 -
    (int16_t)value * ((int16_t)g_chart_height - 1) / 127;
}

static void scope_update_layout(void) {
  if (!g_screen || !g_scene_label) return;

  if (g_legend_panel) {
    lv_obj_update_layout(g_legend_panel);
    int16_t legend_bottom = lv_obj_get_y(g_legend_panel) + lv_obj_get_height(g_legend_panel);
    g_chart_top = legend_bottom + SCOPE_CHART_TOP_GAP;
  } else {
    g_chart_top = 40;
  }

  lv_obj_update_layout(g_scene_label);
  int16_t scene_top = lv_obj_get_y(g_scene_label);
  int16_t chart_bottom = scene_top - SCOPE_CHART_BOTTOM_GAP;
  int16_t height = chart_bottom - g_chart_top;
  if (height < 16) height = 16;
  g_chart_height = (uint16_t)height;

  if (g_plot_area) {
    lv_obj_set_size(g_plot_area, g_chart_width, g_chart_height);
    lv_obj_set_pos(g_plot_area, 0, g_chart_top);
  }
  if (g_chart) {
    lv_obj_set_size(g_chart, g_chart_width, g_chart_height);
    lv_obj_set_pos(g_chart, 0, 0);
  }
}

static uint16_t scope_point_count(void) {
  if (g_point_count >= 16) return g_point_count;
  return 16;
}

static void scope_sync_point_count(void) {
  if (!g_chart || g_point_count < 16) return;
  if (lv_chart_get_point_count(g_chart) != g_point_count)
    lv_chart_set_point_count(g_chart, g_point_count);
}

static uint32_t scope_bar_duration_ms(uint16_t bpm_x10) {
  if (bpm_x10 < TEMPO_MIN_BPM_X10) bpm_x10 = tempo_get_bpm_x10();
  uint8_t felt = tempo_get_felt_beats_per_bar();
  if (felt == 0) felt = 4;
  return (600000UL * felt) / bpm_x10;
}

static uint16_t scope_compute_sample_ms(void) {
  uint16_t points = scope_point_count();
  if (points < 16) return SCOPE_SAMPLE_MS;
  uint32_t bar_ms = scope_bar_duration_ms(tempo_get_bpm_x10());
  uint32_t sample_ms = (bar_ms + points / 2) / points;
  if (sample_ms < 1) sample_ms = 1;
  return (uint16_t)sample_ms;
}

static void scope_update_sample_period(void) {
  g_bpm_x10 = tempo_get_bpm_x10();
  scope_sync_point_count();
  uint16_t points = scope_point_count();
  uint16_t period = scope_compute_sample_ms();
  if (g_sample_timer) {
    lv_timer_set_period(g_sample_timer, period);
    lv_timer_reset(g_sample_timer);
  }
  ESP_LOGD(TAG, "Sample period %u ms (bar %lu ms, %u pts)",
    (unsigned)period, (unsigned long)scope_bar_duration_ms(g_bpm_x10),
    (unsigned)points);
}

static void scope_tempo_handler(const event_t *event, void *context) {
  (void)event;
  (void)context;
  if (!g_module_active) return;
  g_tempo_timing_dirty = true;
}

static void scope_update_value_labels(const scene_t *scene, uint8_t scene_index,
  const uint8_t *values) {
  if (!scene || g_value_font_h <= 0) return;

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    if (!g_value_labels[i]) continue;
    const scope_channel_t *chn = scene_get_scope_channel(scene_index, i);
    if (!chn || chn->kind == SCOPE_SRC_NONE) {
      lv_obj_add_flag(g_value_labels[i], LV_OBJ_FLAG_HIDDEN);
      g_last_value_shown[i] = 0xFF;
      continue;
    }

    uint8_t value = values ? values[i] : scope_get_channel_value(scene, chn);
    int16_t line_y = scope_value_to_line_y(value);
    int16_t label_y = line_y - g_value_font_h / 2;
    if (label_y < g_chart_top) label_y = g_chart_top;
    int16_t max_y = g_chart_top + (int16_t)g_chart_height - g_value_font_h;
    if (label_y > max_y) label_y = max_y;

    if (value != g_last_value_shown[i]) {
      snprintf(g_value_text[i], sizeof(g_value_text[i]), "%u", (unsigned)value);
      lv_label_set_text(g_value_labels[i], g_value_text[i]);
      g_last_value_shown[i] = value;
    }

    lv_obj_set_pos(g_value_labels[i], (int16_t)g_chart_width + SCOPE_VALUE_X_OFFSET, label_y);
    lv_obj_remove_flag(g_value_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void scope_legend_reparent(int ch, lv_obj_t *row, int32_t index) {
  if (!g_legend_rows[ch] || !row) return;
  if (lv_obj_get_parent(g_legend_rows[ch]) != row)
    lv_obj_set_parent(g_legend_rows[ch], row);
  lv_obj_move_to_index(g_legend_rows[ch], index);
}

static void scope_update_legend(void) {
  uint8_t scene_index = scene_get_current_index();
  int active[SCOPE_CHANNEL_COUNT];
  int n = 0;

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    const scope_channel_t *chn = scene_get_scope_channel(scene_index, i);
    if (chn && chn->kind != SCOPE_SRC_NONE)
      active[n++] = i;
  }

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    if (!g_legend_rows[i]) continue;
    const scope_channel_t *chn = scene_get_scope_channel(scene_index, i);
    bool is_active = chn && chn->kind != SCOPE_SRC_NONE;
    if (!is_active) {
      lv_obj_add_flag(g_legend_rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(g_legend_rows[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g_legend_swatches[i], lv_color_hex(scope_colors[i]), 0);
    char name[16];
    scope_channel_label(chn, name, sizeof(name));
    lv_label_set_text(g_legend_labels[i], name);
  }

  if (!g_legend_panel) return;

  if (n == 0) {
    lv_obj_add_flag(g_legend_panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(g_legend_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_legend_row[0], LV_OBJ_FLAG_HIDDEN);

  if (n == 1) {
    scope_legend_reparent(active[0], g_legend_row[0], 0);
    lv_obj_add_flag(g_legend_row[1], LV_OBJ_FLAG_HIDDEN);
  } else if (n == 2) {
    scope_legend_reparent(active[0], g_legend_row[0], 0);
    scope_legend_reparent(active[1], g_legend_row[0], 1);
    lv_obj_add_flag(g_legend_row[1], LV_OBJ_FLAG_HIDDEN);
  } else if (n == 3) {
    lv_obj_remove_flag(g_legend_row[1], LV_OBJ_FLAG_HIDDEN);
    scope_legend_reparent(active[0], g_legend_row[0], 0);
    scope_legend_reparent(active[1], g_legend_row[1], 0);
    scope_legend_reparent(active[2], g_legend_row[1], 1);
  } else {
    lv_obj_remove_flag(g_legend_row[1], LV_OBJ_FLAG_HIDDEN);
    scope_legend_reparent(active[0], g_legend_row[0], 0);
    scope_legend_reparent(active[1], g_legend_row[0], 1);
    scope_legend_reparent(active[2], g_legend_row[1], 0);
    scope_legend_reparent(active[3], g_legend_row[1], 1);
  }

  lv_obj_update_layout(g_legend_panel);
}

static void scope_sync_channel_display(uint8_t scene_index) {
  if (!g_chart) return;

  bool changed = !g_channel_cache_valid;
  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    const scope_channel_t *chn = scene_get_scope_channel(scene_index, i);
    uint8_t kind = chn ? chn->kind : SCOPE_SRC_NONE;
    uint8_t id = chn ? chn->id : 0;
    if (kind == g_cached_kind[i] && id == g_cached_id[i]) continue;

    g_cached_kind[i] = kind;
    g_cached_id[i] = id;
    changed = true;

    if (!g_series[i]) continue;
    bool active = kind != SCOPE_SRC_NONE;
    lv_chart_hide_series(g_chart, g_series[i], !active);
    if (active) {
      const scene_t *scene = scene_get_current();
      uint8_t seed = (scene && chn) ? scope_get_channel_value(scene, chn) : 0;
      lv_chart_set_all_values(g_chart, g_series[i], seed);
    }
  }

  if (!changed) return;

  g_channel_cache_valid = true;
  scope_update_legend();
  scope_update_layout();
  lv_chart_refresh(g_chart);
}

static void scope_update_scene_label(void) {
  if (!g_scene_label) return;

  scene_mode_t mode = scene_get_mode();
  uint8_t scene_index = scene_get_current_index();
  const scene_t *scene = scene_get_current();
  const char *name = (scene && scene->name[0]) ? scene->name : "Untitled";

  if (mode == SCENE_MODE_SINGLE) {
    snprintf(g_scene_info_text, sizeof(g_scene_info_text), "%.20s", name);
  } else {
    uint16_t ordinal = 0;
    uint16_t total = scene_get_total_count();
    for (uint16_t i = 0; i < total; i++) {
      if (scene_is_active_by_position(i)) ordinal++;
      if (scene_get_index_by_position(i) == scene_index) break;
    }
    snprintf(g_scene_info_text, sizeof(g_scene_info_text), "%u. %.16s",
      (unsigned)ordinal, name);
  }
  lv_label_set_text(g_scene_label, g_scene_info_text);
  scope_update_layout();
}

static void scope_apply_config(void) {
  if (!g_chart) return;

  g_channel_cache_valid = false;
  scope_sync_channel_display(scene_get_current_index());
  g_scene_info_dirty = true;

  const scene_t *scene = scene_get_current();
  if (scene) scope_update_value_labels(scene, scene_get_current_index(), NULL);
  scope_update_legend();
  scope_update_layout();
  scope_update_sample_period();
}

static void scope_sample_cb(lv_timer_t *timer) {
  (void)timer;
  if (!g_module_active || !g_chart) return;
  if (ui_is_in_screensaver_mode()) return;

  if (g_tempo_timing_dirty) {
    g_tempo_timing_dirty = false;
    scope_update_sample_period();
  }

  if (g_config_dirty) {
    g_config_dirty = false;
    scope_apply_config();
  }

  if (g_scene_info_dirty) {
    g_scene_info_dirty = false;
    scope_update_scene_label();
  }

  const scene_t *scene = scene_get_current();
  if (!scene) return;

  uint8_t scene_index = scene_get_current_index();
  scope_sync_channel_display(scene_index);

  uint8_t values[SCOPE_CHANNEL_COUNT];
  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    if (!g_series[i]) continue;
    const scope_channel_t *chn = scene_get_scope_channel(scene_index, i);
    if (!chn || chn->kind == SCOPE_SRC_NONE) continue;
    values[i] = scope_get_channel_value(scene, chn);
    lv_chart_set_next_value(g_chart, g_series[i], values[i]);
  }
  scope_update_value_labels(scene, scene_index, values);
}

static void scope_scene_changed_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || !g_module_active) return;
  g_config_dirty = true;
  g_scene_info_dirty = true;
  g_tempo_timing_dirty = true;
}

static void scope_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    scope_update_sample_period();
    lv_timer_delete(timer);
    return;
  }

  lv_display_t *disp = lv_display_get_default();
  g_disp_width = lv_display_get_horizontal_resolution(disp);
  g_disp_height = lv_display_get_vertical_resolution(disp);

  uint16_t shorter = (g_disp_width < g_disp_height) ? g_disp_width : g_disp_height;
  uint16_t box = (uint16_t)(shorter * SCOPE_BOX_FRACTION);
  g_chart_width = (uint16_t)((g_disp_width + box) / 2);
  g_point_count = g_chart_width / SCOPE_HISTORY_DIVISOR;
  if (g_point_count < 16) g_point_count = 16;

  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);

  g_plot_area = lv_obj_create(g_screen);
  lv_obj_remove_flag(g_plot_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_plot_area, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_style_bg_opa(g_plot_area, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_plot_area, 0, 0);
  lv_obj_set_style_pad_all(g_plot_area, 0, 0);

  g_legend_panel = lv_obj_create(g_screen);
  lv_obj_remove_flag(g_legend_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_legend_panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_legend_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_legend_panel, 0, 0);
  lv_obj_set_style_pad_all(g_legend_panel, SCOPE_LEGEND_PANEL_PAD, 0);
  lv_obj_set_style_pad_row(g_legend_panel, 2, 0);
  lv_obj_set_flex_flow(g_legend_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(g_legend_panel, LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(g_legend_panel, LV_ALIGN_TOP_MID, SCOPE_LEGEND_PANEL_X, SCOPE_LEGEND_PANEL_Y);
  lv_obj_set_size(g_legend_panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

  for (int r = 0; r < 2; r++) {
    g_legend_row[r] = lv_obj_create(g_legend_panel);
    lv_obj_remove_flag(g_legend_row[r], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_legend_row[r], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_legend_row[r], 0, 0);
    lv_obj_set_style_pad_all(g_legend_row[r], 0, 0);
    lv_obj_set_style_pad_column(g_legend_row[r], SCOPE_LEGEND_COL_GAP, 0);
    lv_obj_set_flex_flow(g_legend_row[r], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_legend_row[r], LV_FLEX_ALIGN_CENTER,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(g_legend_row[r], LV_SIZE_CONTENT, SCOPE_LEGEND_ROW_H);
  }

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    g_legend_rows[i] = lv_obj_create(g_legend_row[0]);
    lv_obj_set_size(g_legend_rows[i], LV_SIZE_CONTENT, SCOPE_LEGEND_ROW_H);
    lv_obj_remove_flag(g_legend_rows[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(g_legend_rows[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_legend_rows[i], 0, 0);
    lv_obj_set_style_pad_all(g_legend_rows[i], 0, 0);
    lv_obj_set_flex_flow(g_legend_rows[i], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_legend_rows[i], LV_FLEX_ALIGN_START,
      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(g_legend_rows[i], 3, 0);

    g_legend_swatches[i] = lv_obj_create(g_legend_rows[i]);
    lv_obj_set_size(g_legend_swatches[i], SCOPE_SWATCH_W, SCOPE_SWATCH_W);
    lv_obj_set_style_radius(g_legend_swatches[i], 1, 0);
    lv_obj_set_style_border_width(g_legend_swatches[i], 0, 0);
    lv_obj_remove_flag(g_legend_swatches[i], LV_OBJ_FLAG_SCROLLABLE);

    g_legend_labels[i] = lv_label_create(g_legend_rows[i]);
    lv_obj_set_style_text_font(g_legend_labels[i], &chalet_ny_14, 0);
    lv_obj_set_style_text_color(g_legend_labels[i], lv_color_white(), 0);
    lv_obj_add_flag(g_legend_rows[i], LV_OBJ_FLAG_HIDDEN);

    g_value_labels[i] = lv_label_create(g_screen);
    lv_obj_set_style_text_font(g_value_labels[i], &chalet_ny_14, 0);
    lv_obj_set_style_text_color(g_value_labels[i], lv_color_hex(scope_colors[i]), 0);
    lv_obj_add_flag(g_value_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  g_value_font_h = (int16_t)chalet_ny_14.line_height;
  memset(g_last_value_shown, 0xFF, sizeof(g_last_value_shown));

  g_chart = lv_chart_create(g_plot_area);
  lv_obj_set_size(g_chart, g_chart_width, 16);
  lv_obj_set_pos(g_chart, 0, 0);
  lv_obj_remove_flag(g_chart, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 127);
  lv_chart_set_update_mode(g_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(g_chart, g_point_count);
  lv_chart_set_div_line_count(g_chart, 5, 0);

  lv_obj_set_style_bg_color(g_chart, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_chart, 0, LV_PART_MAIN);
  lv_obj_set_style_line_color(g_chart, lv_color_hex(0x303030), LV_PART_MAIN);
  lv_obj_set_style_line_width(g_chart, SCOPE_LINE_WIDTH, LV_PART_ITEMS);
  lv_obj_set_style_width(g_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(g_chart, 0, LV_PART_INDICATOR);

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    g_series[i] = lv_chart_add_series(g_chart, lv_color_hex(scope_colors[i]),
      LV_CHART_AXIS_PRIMARY_Y);
  }

  g_scene_label = lv_label_create(g_screen);
  lv_obj_set_style_text_font(g_scene_label, &flyer_venice_20, 0);
  lv_obj_set_style_text_color(g_scene_label, lv_color_white(), 0);
  lv_obj_set_style_text_align(g_scene_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(g_scene_label, g_disp_width - 8);
  lv_obj_align(g_scene_label, LV_ALIGN_BOTTOM_MID, 0, -SCOPE_SCENE_BOTTOM_OFFSET);

  scope_update_layout();
  scope_apply_config();
  scope_update_scene_label();

  for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
    if (g_value_labels[i])
      lv_obj_move_to_index(g_value_labels[i], -1);
  }
  if (g_legend_panel)
    lv_obj_move_to_index(g_legend_panel, -1);

  g_bpm_x10 = tempo_get_bpm_x10();
  g_sample_timer = lv_timer_create(scope_sample_cb, scope_compute_sample_ms(), NULL);
  scope_update_sample_period();

  event_bus_subscribe(EVENT_SCENE_CHANGED, scope_scene_changed_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_REORDERED, scope_scene_changed_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_LIST_CHANGED, scope_scene_changed_handler, NULL);
  event_bus_subscribe(EVENT_TEMPO_CHANGED, scope_tempo_handler, NULL);

  g_module_active = true;

  ESP_LOGI(TAG, "Scope module created - chart %ux%u (%u pts, %u ms/sample)",
    (unsigned)g_chart_width, (unsigned)g_chart_height, (unsigned)g_point_count,
    (unsigned)scope_compute_sample_ms());

  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(scope, scope_draw_deferred_cb)

static void scope_teardown(void) {
  g_module_active = false;

  event_bus_unsubscribe(EVENT_SCENE_CHANGED, scope_scene_changed_handler);
  event_bus_unsubscribe(EVENT_SCENE_REORDERED, scope_scene_changed_handler);
  event_bus_unsubscribe(EVENT_SCENE_LIST_CHANGED, scope_scene_changed_handler);
  event_bus_unsubscribe(EVENT_TEMPO_CHANGED, scope_tempo_handler);

  if (g_sample_timer) {
    lv_timer_delete(g_sample_timer);
    g_sample_timer = NULL;
  }

  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_plot_area = NULL;
    g_chart = NULL;
    g_scene_label = NULL;
    g_legend_panel = NULL;
    g_legend_row[0] = NULL;
    g_legend_row[1] = NULL;
    for (int i = 0; i < SCOPE_CHANNEL_COUNT; i++) {
      g_series[i] = NULL;
      g_legend_rows[i] = NULL;
      g_legend_swatches[i] = NULL;
      g_legend_labels[i] = NULL;
      g_value_labels[i] = NULL;
      g_last_value_shown[i] = 0xFF;
    }
  }

  g_config_dirty = false;
  g_scene_info_dirty = false;
  g_tempo_timing_dirty = false;
  g_channel_cache_valid = false;
}

static void scope_init(void) {}

ui_draw_module_t scope_module = {
  .draw_func = scope_draw,
  .teardown_func = scope_teardown,
  .init_func = scope_init,
  .name = "scope",
  .title = "Scope"
};
