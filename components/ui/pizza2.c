#include "lvgl.h"
#include "ui.h"
#include <math.h>
#include "esp_log.h"

#define PIZZA_CENTER_X 64
#define PIZZA_CENTER_Y 64
#define PIZZA_RADIUS 60

#define SLICE_COUNT 8
#define GRAY_TONE 6
#define DEFAULT_BITE_SIZE 25
#define TAG "PIZZA2"

extern lv_obj_t *canvas;

static void draw_active_filled_slice(lv_layer_t *layer, uint8_t slice_index) {
  float slice_angle_degrees = 360.0f / SLICE_COUNT;
  float start_angle_deg = (float)slice_index * slice_angle_degrees;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;

  float rad_start = start_angle_deg * (M_PI / 180.0f);
  float rad_end = end_angle_deg * (M_PI / 180.0f);

  lv_coord_t center_x_coord = PIZZA_CENTER_X;
  lv_coord_t center_y_coord = PIZZA_CENTER_Y;

  // 1. Draw the filled gray sector using multiple concentric 1px arcs
  if (DEFAULT_BITE_SIZE <= PIZZA_RADIUS) {
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

    for (uint16_t r = DEFAULT_BITE_SIZE; r <= PIZZA_RADIUS; r++) {
      if (r == 0 && DEFAULT_BITE_SIZE == 0) continue;
      fill_arc_dsc.radius = r;
      lv_draw_arc(layer, &fill_arc_dsc);
    }
  }

  lv_point_precise_t start_inner_p = {
    .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_start)),
    .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_start))
  };
  lv_point_precise_t start_outer_p = {
    .x = center_x_coord + (lv_coord_t)(PIZZA_RADIUS * cosf(rad_start)),
    .y = center_y_coord + (lv_coord_t)(PIZZA_RADIUS * sinf(rad_start))
  };
  lv_point_precise_t end_inner_p = {
    .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_end)),
    .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_end))
  };
  lv_point_precise_t end_outer_p = {
    .x = center_x_coord + (lv_coord_t)(PIZZA_RADIUS * cosf(rad_end)),
    .y = center_y_coord + (lv_coord_t)(PIZZA_RADIUS * sinf(rad_end))
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
  outline_arc_dsc.radius = PIZZA_RADIUS; 
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

static void pizza2_draw_deferred_cb(lv_timer_t *timer) {
  ESP_LOGI(TAG, "pizza2_draw_deferred_cb() called");
  
  if (!canvas) {
    ESP_LOGW(TAG, "Canvas is NULL in deferred callback!");
    lv_timer_del(timer);
    return;
  }

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) {
    ESP_LOGW(TAG, "Layer draw_buf is NULL in deferred callback!");
    lv_timer_del(timer);
    return;
  }

  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  bool default_slices[SLICE_COUNT] = {true, false, true, false, true, false, true, false};
  
  for (uint8_t i = 0; i < SLICE_COUNT; i++) {
    if (default_slices[i]) draw_active_filled_slice(&layer, i);
  }
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
  
  ESP_LOGI(TAG, "pizza2_draw_deferred_cb() completed");
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(pizza2, pizza2_draw_deferred_cb)

static void pizza2_teardown(void) {}

static void pizza2_init(void) {}

ui_draw_module_t pizza2_module = {
  .draw_func = pizza2_draw,
  .teardown_func = pizza2_teardown,
  .init_func = pizza2_init,
  .name = "pizza2"
};
