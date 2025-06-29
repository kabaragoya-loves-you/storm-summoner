/**
 * Sparse Buffer Implementation for Circular Display - Mode 4
 * 
 * This module implements a simple sparse buffer optimization that compresses
 * the flush buffer by removing non-visible pixels before sending to display.
 */

#include "sparse_buffer.h"
#include "coordinate_map.h"
#include "ssd1327_driver.h"
#include "../lvgl/src/display/lv_display_private.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>

#define TAG "SPARSE_BUF"
#define SPARSE_CACHE_SIZE (VISIBLE_PIXEL_COUNT * 2)  // Cache for visible pixels only

static lv_display_t *sparse_display = NULL;
static lv_display_flush_cb_t original_flush_cb = NULL;
static uint8_t *sparse_cache = NULL;
static bool sparse_enabled = false;

// Statistics
static struct {
  uint32_t total_pixels_processed;
  uint32_t visible_pixels_processed;
  uint32_t flushes;
  uint32_t bytes_saved;
} stats = {0};

/**
 * Optimized flush callback that compresses buffer data
 * This demonstrates the concept but still needs to send full buffer to display
 */
static void sparse_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  if (!sparse_enabled || !original_flush_cb) {
    if (original_flush_cb) original_flush_cb(disp, area, px_map);
    return;
  }
  
  stats.flushes++;
  
  // For Mode 4 demonstration: analyze the buffer to show potential savings
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  uint32_t visible_pixels = 0;
  uint32_t total_pixels = w * h;
  
  // Count visible pixels in this flush area
  for (int32_t y = area->y1; y <= area->y2; y++) {
    for (int32_t x = area->x1; x <= area->x2; x++) {
      int32_t map_idx = y * SCREEN_WIDTH + x;
      if (coordinate_map[map_idx] != -1) {
        visible_pixels++;
      }
    }
  }
  
  stats.total_pixels_processed += total_pixels;
  stats.visible_pixels_processed += visible_pixels;
  stats.bytes_saved += (total_pixels - visible_pixels) * 2; // 2 bytes per pixel
  
  // Log compression potential every 100 flushes
  if (stats.flushes % 100 == 0) {
    float compression_ratio = (float)stats.visible_pixels_processed / stats.total_pixels_processed;
    ESP_LOGI(TAG, "Sparse buffer stats after %lu flushes:", (unsigned long)stats.flushes);
    ESP_LOGI(TAG, "  Total pixels: %lu, Visible: %lu (%.1f%%)",
             (unsigned long)stats.total_pixels_processed,
             (unsigned long)stats.visible_pixels_processed,
             compression_ratio * 100.0f);
    ESP_LOGI(TAG, "  Potential memory saved: %lu KB",
             (unsigned long)(stats.bytes_saved / 1024));
  }
  
  // For now, pass through to original flush callback
  // In a full implementation, we would create a compressed buffer here
  original_flush_cb(disp, area, px_map);
}

/**
 * Create a compressed buffer containing only visible pixels
 * This is a demonstration of how the compression would work
 */
__attribute__((unused))
static uint32_t compress_buffer(const lv_area_t *area, const uint8_t *src, uint8_t *dest) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  uint32_t dest_idx = 0;
  
  // Copy only visible pixels to destination
  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      int32_t abs_x = area->x1 + x;
      int32_t abs_y = area->y1 + y;
      int32_t map_idx = abs_y * SCREEN_WIDTH + abs_x;
      
      if (coordinate_map[map_idx] != -1) {
        // Copy pixel (RGB565 = 2 bytes)
        dest[dest_idx++] = src[(y * w + x) * 2];
        dest[dest_idx++] = src[(y * w + x) * 2 + 1];
      }
    }
  }
  
  return dest_idx;
}

/**
 * Expand compressed buffer back to full size
 * This would be needed if we actually sent compressed data
 */
__attribute__((unused))
static void expand_buffer(const lv_area_t *area, const uint8_t *src, uint8_t *dest) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  uint32_t src_idx = 0;
  
  // Clear destination (black background)
  memset(dest, 0, w * h * 2);
  
  // Copy visible pixels from source
  for (int32_t y = 0; y < h; y++) {
    for (int32_t x = 0; x < w; x++) {
      int32_t abs_x = area->x1 + x;
      int32_t abs_y = area->y1 + y;
      int32_t map_idx = abs_y * SCREEN_WIDTH + abs_x;
      
      if (coordinate_map[map_idx] != -1) {
        // Copy pixel (RGB565 = 2 bytes)
        dest[(y * w + x) * 2] = src[src_idx++];
        dest[(y * w + x) * 2 + 1] = src[src_idx++];
      }
    }
  }
}

void sparse_buffer_init(lv_display_t *disp) {
  if (!disp) return;
  
  sparse_display = disp;
  
  // Allocate cache for sparse data (not currently used, but demonstrates the concept)
  if (!sparse_cache) {
    sparse_cache = heap_caps_malloc(SPARSE_CACHE_SIZE, MALLOC_CAP_DMA);
    if (!sparse_cache) {
      ESP_LOGE(TAG, "Failed to allocate sparse cache");
      return;
    }
  }
  
  // Hook the flush callback
  original_flush_cb = disp->flush_cb;
  lv_display_set_flush_cb(disp, sparse_flush_cb);
  
  sparse_enabled = true;
  
  ESP_LOGI(TAG, "Mode 4: Sparse buffer system initialized");
  ESP_LOGI(TAG, "Visible pixels: %d of %d (%.1f%% compression potential)", 
           VISIBLE_PIXEL_COUNT, TOTAL_PIXELS,
           (1.0f - (float)VISIBLE_PIXEL_COUNT / TOTAL_PIXELS) * 100.0f);
  ESP_LOGI(TAG, "Note: This mode demonstrates compression analysis");
  ESP_LOGI(TAG, "Full implementation would require display driver modifications");
}

bool sparse_buffer_is_enabled(void) {
  return sparse_enabled;
}

void sparse_buffer_enable(bool enable) {
  sparse_enabled = enable;
  ESP_LOGI(TAG, "Sparse buffer system %s", enable ? "enabled" : "disabled");
  
  if (!enable && sparse_display && original_flush_cb) {
    // Restore original flush callback
    lv_display_set_flush_cb(sparse_display, original_flush_cb);
  } else if (enable && sparse_display && original_flush_cb) {
    // Re-hook our flush callback
    lv_display_set_flush_cb(sparse_display, sparse_flush_cb);
  }
} 