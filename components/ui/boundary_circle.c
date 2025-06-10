#include "lvgl.h"
#include "ui.h"

extern lv_obj_t *canvas;

void boundary_circle(void) {
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
  arc_dsc.center.x = 64;  // Center of 128x128 canvas
  arc_dsc.center.y = 64;
  arc_dsc.radius = 63;    // Almost full radius to fit in 128x128
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(&layer, &arc_dsc);
  
  lv_canvas_finish_layer(canvas, &layer);
  lv_obj_invalidate(canvas);
}
