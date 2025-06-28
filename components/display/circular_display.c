#include "circular_display.h"
#include "coordinate_map.h"
#include "ssd1327_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include "../lvgl/src/display/lv_display_private.h"

#define TAG "CIRCULAR_DISPLAY"

// Store the circular display instance (single instance for now)
static circular_display_t* g_circular_display = NULL;

// Check if a pixel is within the circular display area
static inline bool is_pixel_in_circle(int16_t x, int16_t y) {
  int16_t dx = x - CIRCULAR_DISPLAY_CENTER_X;
  int16_t dy = y - CIRCULAR_DISPLAY_CENTER_Y;
  return (dx * dx + dy * dy) <= (CIRCULAR_DISPLAY_RADIUS * CIRCULAR_DISPLAY_RADIUS);
}

// Custom flush implementation that filters out invisible pixels
void circular_display_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  if (!g_circular_display) {
    ESP_LOGE(TAG, "Circular display not properly initialized");
    return;
  }
  
  // Quick check if entire area is outside circle
  int32_t closest_x = (area->x1 > CIRCULAR_DISPLAY_CENTER_X) ? area->x1 : 
            ((area->x2 < CIRCULAR_DISPLAY_CENTER_X) ? area->x2 : CIRCULAR_DISPLAY_CENTER_X);
  int32_t closest_y = (area->y1 > CIRCULAR_DISPLAY_CENTER_Y) ? area->y1 : 
            ((area->y2 < CIRCULAR_DISPLAY_CENTER_Y) ? area->y2 : CIRCULAR_DISPLAY_CENTER_Y);
  
  int32_t dx = closest_x - CIRCULAR_DISPLAY_CENTER_X;
  int32_t dy = closest_y - CIRCULAR_DISPLAY_CENTER_Y;
  
  if ((dx * dx + dy * dy) > (CIRCULAR_DISPLAY_RADIUS * CIRCULAR_DISPLAY_RADIUS)) {
    // Entire area is outside circle - create black buffer
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    static uint8_t* black_buffer = NULL;
    static size_t black_buffer_size = 0;
    
    size_t needed_size = w * h * 2; // RGB565
    if (!black_buffer || black_buffer_size < needed_size) {
      if (black_buffer) heap_caps_free(black_buffer);
      black_buffer = heap_caps_calloc(1, needed_size, MALLOC_CAP_DMA);
      black_buffer_size = needed_size;
    }
    
    // Call the original ssd1327 flush directly
    ssd1327_flush(disp, area, black_buffer);
    return;
  }
  
  // For areas that intersect the circle, we need to filter pixels
  // For now, pass through to original flush which does per-pixel checking
  ssd1327_flush(disp, area, px_map);
}

circular_display_t* circular_display_init(lv_display_t* disp) {
  if (g_circular_display) {
    ESP_LOGW(TAG, "Circular display already initialized");
    return g_circular_display;
  }
  
  g_circular_display = (circular_display_t*)heap_caps_calloc(1, sizeof(circular_display_t), MALLOC_CAP_DEFAULT);
  if (!g_circular_display) {
    ESP_LOGE(TAG, "Failed to allocate circular display structure");
    return NULL;
  }
  
  g_circular_display->disp = disp;
  g_circular_display->coord_map = coordinate_map;
  
  // Install our custom flush callback
  lv_display_set_flush_cb(disp, circular_display_flush);
  
  ESP_LOGI(TAG, "Circular display optimization initialized");
  
  return g_circular_display;
}

void circular_display_deinit(circular_display_t* circular_disp) {
  if (!circular_disp) return;
  
  // Restore original flush callback - we know it's ssd1327_flush
  if (circular_disp->disp) {
    lv_display_set_flush_cb(circular_disp->disp, ssd1327_flush);
  }
  
  // Free sparse buffers if allocated
  if (circular_disp->sparse_buf1) heap_caps_free(circular_disp->sparse_buf1);
  if (circular_disp->sparse_buf2) heap_caps_free(circular_disp->sparse_buf2);
  
  heap_caps_free(circular_disp);
  g_circular_display = NULL;
} 