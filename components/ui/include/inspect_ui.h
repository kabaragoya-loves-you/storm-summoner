#ifndef INSPECT_UI_H
#define INSPECT_UI_H

#include "lvgl.h"
#include <stdint.h>

// Build opaque inspect panel (title bar + scrollable wrapped label) on parent.
// out_scroll_cont receives the scroll container for programmatic scrolling.
lv_obj_t *inspect_ui_create(lv_obj_t *parent, const char *text, uint8_t scene_index,
  lv_obj_t **out_scroll_cont);

// Line step and top padding for pad jog scroll (programming Inspect only).
void inspect_ui_cache_scroll_metrics(lv_obj_t *label, lv_obj_t *scroll_cont,
  int32_t *out_line_step_px, int32_t *out_pad_top_px);

#endif // INSPECT_UI_H
