#include "shared_canvas_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "SHARED_BUF"

// Single persistent buffer - allocated once, never freed during normal operation
static void *s_shared_buffer = NULL;
static size_t s_buffer_size = 0;

bool shared_canvas_buffer_init(void) {
  if (s_shared_buffer != NULL) {
    ESP_LOGW(TAG, "Shared buffer already initialized");
    return true;
  }

  // Calculate buffer size based on native color format
  size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
  s_buffer_size = SHARED_CANVAS_WIDTH * SHARED_CANVAS_HEIGHT * bytes_per_pixel;

  ESP_LOGI(TAG, "Allocating shared canvas buffer: %dx%d, %d bytes/pixel, total %d bytes",
    SHARED_CANVAS_WIDTH, SHARED_CANVAS_HEIGHT, bytes_per_pixel, s_buffer_size);

  // Allocate with 64-byte alignment for PPA hardware acceleration
  // Use internal RAM for best performance
  s_shared_buffer = heap_caps_aligned_alloc(64, s_buffer_size, 
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_shared_buffer) {
    ESP_LOGE(TAG, "Failed to allocate shared canvas buffer!");
    s_buffer_size = 0;
    return false;
  }

  // Initialize to black
  memset(s_shared_buffer, 0, s_buffer_size);

  ESP_LOGI(TAG, "Shared canvas buffer allocated: %d KB at %p (64-byte aligned)",
    s_buffer_size / 1024, s_shared_buffer);

  return true;
}

void *shared_canvas_buffer_get(void) {
  return s_shared_buffer;
}

size_t shared_canvas_buffer_get_size(void) {
  return s_buffer_size;
}

lv_color_format_t shared_canvas_buffer_get_format(void) {
  return LV_COLOR_FORMAT_NATIVE;
}

bool shared_canvas_buffer_is_valid(void) {
  return s_shared_buffer != NULL && s_buffer_size > 0;
}

void shared_canvas_buffer_clear(void) {
  if (s_shared_buffer && s_buffer_size > 0) {
    memset(s_shared_buffer, 0, s_buffer_size);
  }
}

