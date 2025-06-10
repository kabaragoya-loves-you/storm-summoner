#include "lvgl.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"

lv_obj_t *canvas = NULL;
static lv_obj_t *circle = NULL;
static lv_timer_t *g_ui_refresh_timer = NULL;

static lv_color_t display_buf[128 * 128] __attribute__((aligned(4)));

#define CENTER_X 64
#define CENTER_Y 64
#define RADIUS 60
#define TAG "UI"

#define SLICE_COUNT 8
#define GRAY_TONE 6
#define DEFAULT_BITE_SIZE 25

app_mode_t g_app_mode = APP_MODE_PERFORMANCE;
bool g_at_programming_top_level_menu = false;

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  if (canvas != NULL) lv_obj_invalidate(canvas);
}

static void draw_active_filled_slice(lv_layer_t *layer, uint8_t slice_index) {
  float slice_angle_degrees = 360.0f / SLICE_COUNT;
  float start_angle_deg = (float)slice_index * slice_angle_degrees;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;

  float rad_start = start_angle_deg * (M_PI / 180.0f);
  float rad_end = end_angle_deg * (M_PI / 180.0f);

  lv_coord_t center_x_coord = CENTER_X;
  lv_coord_t center_y_coord = CENTER_Y;

  // 1. Draw the filled gray sector using multiple concentric 1px arcs
  if (DEFAULT_BITE_SIZE <= RADIUS) {
    lv_draw_arc_dsc_t fill_arc_dsc;
    lv_draw_arc_dsc_init(&fill_arc_dsc);
    uint8_t actual_gray_value_for_lvgl = (GRAY_TONE * 255) / 15;
    fill_arc_dsc.color = lv_color_make(actual_gray_value_for_lvgl, actual_gray_value_for_lvgl, actual_gray_value_for_lvgl);
    fill_arc_dsc.width = 1; // Each concentric arc is 1px wide
    fill_arc_dsc.opa = LV_OPA_COVER;
    fill_arc_dsc.center.x = center_x_coord;
    fill_arc_dsc.center.y = center_y_coord;
    fill_arc_dsc.start_angle = (int32_t)start_angle_deg;
    fill_arc_dsc.end_angle = (int32_t)end_angle_deg;
    fill_arc_dsc.rounded = 0;

    ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Filling with concentric arcs from r=%d to r=%d", slice_index, DEFAULT_BITE_SIZE, RADIUS);
    for (uint16_t r = DEFAULT_BITE_SIZE; r <= RADIUS; r++) {
      if (r == 0 && DEFAULT_BITE_SIZE == 0) { /*ESP_LOGD(TAG, "Skipping r=0 for full fill");*/ continue; }
      fill_arc_dsc.radius = r;
      lv_draw_arc(layer, &fill_arc_dsc);
    }
    ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Concentric fill done.", slice_index);
  }

  lv_point_precise_t start_inner_p = {
    .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_start)),
    .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_start))
  };
  lv_point_precise_t start_outer_p = {
    .x = center_x_coord + (lv_coord_t)(RADIUS * cosf(rad_start)),
    .y = center_y_coord + (lv_coord_t)(RADIUS * sinf(rad_start))
  };
  lv_point_precise_t end_inner_p = {
    .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_end)),
    .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_end))
  };
  lv_point_precise_t end_outer_p = {
    .x = center_x_coord + (lv_coord_t)(RADIUS * cosf(rad_end)),
    .y = center_y_coord + (lv_coord_t)(RADIUS * sinf(rad_end))
  };

  // 2. Draw white radial lines on top of the fill
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_white();
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_COVER;

  line_dsc.p1 = start_inner_p;
  line_dsc.p2 = start_outer_p;
  lv_draw_line(layer, &line_dsc);

  line_dsc.p1 = end_inner_p;
  line_dsc.p2 = end_outer_p;
  lv_draw_line(layer, &line_dsc);

  // 3. Draw white outer arc segment (outline) on top of the fill
  lv_draw_arc_dsc_t outline_arc_dsc;
  lv_draw_arc_dsc_init(&outline_arc_dsc);
  outline_arc_dsc.color = lv_color_white();
  outline_arc_dsc.width = 1; 
  outline_arc_dsc.opa = LV_OPA_COVER;
  outline_arc_dsc.center.x = center_x_coord;
  outline_arc_dsc.center.y = center_y_coord;
  outline_arc_dsc.radius = RADIUS; 
  outline_arc_dsc.start_angle = (int32_t)start_angle_deg;
  outline_arc_dsc.end_angle = (int32_t)end_angle_deg;
  outline_arc_dsc.rounded = 0; 
  lv_draw_arc(layer, &outline_arc_dsc);

  // 4. Draw white inner arc segment (bite outline) if BITE_SIZE > 0
  if (DEFAULT_BITE_SIZE > 0) {
    lv_draw_arc_dsc_t bite_outline_arc_dsc;
    lv_draw_arc_dsc_init(&bite_outline_arc_dsc);
    bite_outline_arc_dsc.color = lv_color_white();
    bite_outline_arc_dsc.width = 1;
    bite_outline_arc_dsc.opa = LV_OPA_COVER;
    bite_outline_arc_dsc.center.x = center_x_coord;
    bite_outline_arc_dsc.center.y = center_y_coord;
    bite_outline_arc_dsc.radius = DEFAULT_BITE_SIZE;
    bite_outline_arc_dsc.start_angle = (int32_t)start_angle_deg;
    bite_outline_arc_dsc.end_angle = (int32_t)end_angle_deg;
    bite_outline_arc_dsc.rounded = 0;
    lv_draw_arc(layer, &bite_outline_arc_dsc);
  }
}

void pizza2(const bool slice_states[SLICE_COUNT]) {
  if (!canvas) return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) return;

  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  for (uint8_t i = 0; i < SLICE_COUNT; i++) {
    if (slice_states[i]) draw_active_filled_slice(&layer, i);
  }
  
  lv_canvas_finish_layer(canvas, &layer);
}

static void deferred_pizza2_cb(lv_timer_t *timer) {  
  bool slices[SLICE_COUNT] = {true, false, true, false, true, false, true, false};
  pizza2(slices);
  
  lv_timer_del(timer);
}

static void deferred_canvas_hide_cb(lv_timer_t *timer) {
  if (canvas != NULL) lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_timer_del(timer);
}

static void deferred_canvas_show_cb(lv_timer_t *timer) {
  if (canvas != NULL) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(canvas);
    ESP_LOGD(TAG, "Canvas shown and invalidated (deferred)");
  }
  lv_timer_del(timer);
}

void ui_init(void) {
  canvas = lv_canvas_create(lv_scr_act());

  lv_obj_remove_style_all(canvas);

  lv_canvas_set_buffer(canvas, display_buf, 128, 128, LV_COLOR_FORMAT_NATIVE);

  lv_obj_set_size(canvas, 128, 128);
  lv_obj_center(canvas);

  lv_timer_t *deferred_timer = lv_timer_create(deferred_pizza2_cb, 50, NULL);
  if (deferred_timer != NULL) lv_timer_set_repeat_count(deferred_timer, 1);
  
  g_ui_refresh_timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps

  g_app_mode = APP_MODE_PERFORMANCE;
  g_at_programming_top_level_menu = false;
  ESP_LOGI(TAG, "UI initialized");
}

app_mode_t ui_get_app_mode(void) {
  return g_app_mode;
}

void ui_set_app_mode(app_mode_t mode) {
  g_app_mode = mode;
  ESP_LOGI(TAG, "App mode set to: %s", mode == APP_MODE_PERFORMANCE ? "Performance" : "Programming");
}

bool ui_is_programming_top_level(void) {
  return g_at_programming_top_level_menu;
}

void ui_set_programming_top_level(bool is_top_level) {
  g_at_programming_top_level_menu = is_top_level;
  ESP_LOGI(TAG, "Programming menu level set to: %s", is_top_level ? "Top Level" : "Sub-Level");
}

void ui_graphics_suspend(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_pause(g_ui_refresh_timer);

  // Defer canvas hiding to avoid blocking in timer callback context
  lv_timer_t *hide_timer = lv_timer_create(deferred_canvas_hide_cb, 1, NULL);
  if (hide_timer != NULL) lv_timer_set_repeat_count(hide_timer, 1);
}

void ui_graphics_resume(void) {
  if (g_ui_refresh_timer != NULL) lv_timer_resume(g_ui_refresh_timer);

  // Defer canvas showing to avoid blocking in timer callback context
  lv_timer_t *show_timer = lv_timer_create(deferred_canvas_show_cb, 1, NULL);
  if (show_timer != NULL) lv_timer_set_repeat_count(show_timer, 1);
}
