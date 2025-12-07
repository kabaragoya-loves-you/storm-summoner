#ifndef LVGL_STREAM_H
#define LVGL_STREAM_H

#include "esp_err.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

// Protocol constants
#define LVGL_STREAM_MAGIC      0xAC01
#define LVGL_STREAM_TYPE_RECT  0x00
#define LVGL_STREAM_FMT_RGB888 0x00

// Wire protocol header (16 bytes, packed)
typedef struct __attribute__((packed)) {
  uint16_t magic;        // 0xAC01
  uint8_t  type;         // 0 = RECT
  uint8_t  format;       // 0 = RGB888
  uint16_t x;            // Top-left X
  uint16_t y;            // Top-left Y
  uint16_t w;            // Width
  uint16_t h;            // Height
  uint32_t payload_len;  // w * h * bytes_per_pixel
} lvgl_stream_hdr_t;

esp_err_t lvgl_stream_init(void);
void lvgl_stream_set_dimensions(uint16_t width, uint16_t height);
esp_err_t lvgl_stream_start(void);
void lvgl_stream_stop(void);
bool lvgl_stream_is_active(void);
esp_err_t lvgl_stream_queue_flush(const lv_area_t *area, const uint8_t *px_map);
void lvgl_stream_get_dimensions(uint16_t *width, uint16_t *height);

#endif // LVGL_STREAM_H
