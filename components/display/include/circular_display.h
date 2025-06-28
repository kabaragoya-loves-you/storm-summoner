#ifndef CIRCULAR_DISPLAY_H
#define CIRCULAR_DISPLAY_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

// Circular display configuration
#define CIRCULAR_DISPLAY_RADIUS 64
#define CIRCULAR_DISPLAY_CENTER_X 64
#define CIRCULAR_DISPLAY_CENTER_Y 64

// Custom display driver data
typedef struct {
  lv_display_t* disp;
  void* original_buf1;
  void* original_buf2;
  void* sparse_buf1;
  void* sparse_buf2;
  uint32_t sparse_buf_size;
  const int16_t* coord_map;
} circular_display_t;

// Initialize circular display optimization
circular_display_t* circular_display_init(lv_display_t* disp);

// Custom flush callback that handles circular clipping
void circular_display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

// Clean up circular display resources
void circular_display_deinit(circular_display_t* circular_disp);

#endif // CIRCULAR_DISPLAY_H 