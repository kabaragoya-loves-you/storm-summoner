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

// Create pizza slice with curved crust and hybrid smooth bite
static void draw_active_filled_slice(lv_layer_t *layer, uint8_t slice_index) {
  float slice_angle_degrees = 360.0f / SLICE_COUNT;
  float start_angle_deg = (float)slice_index * slice_angle_degrees - 90.0f;
  float end_angle_deg = start_angle_deg + slice_angle_degrees;

  uint8_t actual_gray_value_for_lvgl = (GRAY_TONE * 255) / 15;
  lv_color_t fill_color = lv_color_make(actual_gray_value_for_lvgl, actual_gray_value_for_lvgl, actual_gray_value_for_lvgl);
  
  // 🍕 Create polygon vertices for pizza slice with rough bite
  polygon_point_t vertices[POLYGON_MAX_VERTICES];
  int vertex_count = 0;
  
  // Outer arc (CURVED CRUST - back to proper radius)
  vertex_count += polygon_create_arc(&vertices[vertex_count], 
                                    PIZZA_CENTER_X, PIZZA_CENTER_Y, 
                                    PIZZA_RADIUS, start_angle_deg, end_angle_deg, 
                                    8, false);
  
  // Inner arc (rough bite curve for polygon fill)
  vertex_count += polygon_create_arc(&vertices[vertex_count], 
                                    PIZZA_CENTER_X, PIZZA_CENTER_Y, 
                                    DEFAULT_BITE_SIZE, start_angle_deg, end_angle_deg, 
                                    4, true); // Fewer steps - rough is OK, arc will smooth it
  
  // 🎯 Rasterize the polygon!
  polygon_fill(canvas, vertices, vertex_count, fill_color);
  
  // 🎯 HYBRID APPROACH: Only smooth the bite edge (crust is naturally smooth from display edge)
  
  // Smooth bite arc (inner edge) - only if bite > 0
  if (DEFAULT_BITE_SIZE > 0) {
    lv_draw_arc_dsc_t bite_arc_dsc;
    lv_draw_arc_dsc_init(&bite_arc_dsc);
    bite_arc_dsc.color = lv_color_black();
    bite_arc_dsc.width = 3;  // Thin arc for clean edge
    bite_arc_dsc.opa = LV_OPA_COVER;
    bite_arc_dsc.center.x = PIZZA_CENTER_X;
    bite_arc_dsc.center.y = PIZZA_CENTER_Y;
    bite_arc_dsc.radius = DEFAULT_BITE_SIZE;
    bite_arc_dsc.start_angle = (int16_t)start_angle_deg;
    bite_arc_dsc.end_angle = (int16_t)end_angle_deg;
    lv_draw_arc(layer, &bite_arc_dsc);
  }
}

static void pizza2_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_del(timer);
    return;
  }

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) {
    lv_timer_del(timer);
    return;
  }

  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  bool default_slices[SLICE_COUNT] = {true, false, true, false, true, false, true, false};
  
  for (uint8_t i = 0; i < SLICE_COUNT; i++) {
    if (default_slices[i]) {
      draw_active_filled_slice(&layer, i);
    }
  }
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
  
  lv_timer_del(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(pizza2, pizza2_draw_deferred_cb)

static void pizza2_teardown(void) {
  // No teardown needed
}

static void pizza2_init(void) {
  // No init needed
}

ui_draw_module_t pizza2_module = {
  .draw_func = pizza2_draw,
  .teardown_func = pizza2_teardown,
  .init_func = pizza2_init,
  .name = "pizza2"
};
