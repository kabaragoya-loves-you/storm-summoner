#include "lvgl.h"
#include "ui.h"

#define PIZZA_CENTER_X 64
#define PIZZA_CENTER_Y 64
#define PIZZA_RADIUS 60

extern lv_obj_t *canvas;

void pizza(void) {
  if (!canvas) return;

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) return;
  
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_white();
  arc_dsc.width = 1;
  arc_dsc.opa = LV_OPA_COVER;
  arc_dsc.center.x = PIZZA_CENTER_X;
  arc_dsc.center.y = PIZZA_CENTER_Y;
  arc_dsc.radius = PIZZA_RADIUS;
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(&layer, &arc_dsc);
  
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_white();
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_COVER;
  
  line_dsc.p1.x = PIZZA_CENTER_X - PIZZA_RADIUS;
  line_dsc.p1.y = PIZZA_CENTER_Y;
  line_dsc.p2.x = PIZZA_CENTER_X + PIZZA_RADIUS;
  line_dsc.p2.y = PIZZA_CENTER_Y;
  lv_draw_line(&layer, &line_dsc);
  
  line_dsc.p1.x = PIZZA_CENTER_X;
  line_dsc.p1.y = PIZZA_CENTER_Y - PIZZA_RADIUS;
  line_dsc.p2.x = PIZZA_CENTER_X;
  line_dsc.p2.y = PIZZA_CENTER_Y + PIZZA_RADIUS;
  lv_draw_line(&layer, &line_dsc);
  
  line_dsc.p1.x = PIZZA_CENTER_X - (int)(PIZZA_RADIUS * 0.7071); // cos(45deg) = 0.7071
  line_dsc.p1.y = PIZZA_CENTER_Y - (int)(PIZZA_RADIUS * 0.7071); // sin(45deg) = 0.7071
  line_dsc.p2.x = PIZZA_CENTER_X + (int)(PIZZA_RADIUS * 0.7071);
  line_dsc.p2.y = PIZZA_CENTER_Y + (int)(PIZZA_RADIUS * 0.7071);
  lv_draw_line(&layer, &line_dsc);
  
  line_dsc.p1.x = PIZZA_CENTER_X + (int)(PIZZA_RADIUS * 0.7071);
  line_dsc.p1.y = PIZZA_CENTER_Y - (int)(PIZZA_RADIUS * 0.7071);
  line_dsc.p2.x = PIZZA_CENTER_X - (int)(PIZZA_RADIUS * 0.7071);
  line_dsc.p2.y = PIZZA_CENTER_Y + (int)(PIZZA_RADIUS * 0.7071);
  lv_draw_line(&layer, &line_dsc);
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
} 