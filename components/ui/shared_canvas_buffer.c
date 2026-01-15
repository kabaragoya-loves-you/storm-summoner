#include "shared_canvas_buffer.h"
#include "display_driver.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "SHARED_BUF"

// Single persistent buffer - allocated once, never freed during normal operation
static void *s_shared_buffer = NULL;
static size_t s_buffer_size = 0;
static uint16_t s_width = 0;
static uint16_t s_height = 0;
static lv_color_format_t s_color_format = LV_COLOR_FORMAT_RGB565;

bool shared_canvas_buffer_init(void) {
  if (s_shared_buffer != NULL) {
    ESP_LOGW(TAG, "Shared buffer already initialized");
    return true;
  }

  // Get dimensions and format from display driver
  s_width = display_get_width();
  s_height = display_get_height();
  s_color_format = display_get_color_format();
  
  // Calculate buffer size based on display dimensions and color format
  size_t bytes_per_pixel = lv_color_format_get_size(s_color_format);
  s_buffer_size = s_width * s_height * bytes_per_pixel;

  ESP_LOGI(TAG, "Allocating shared canvas buffer: %dx%d, %zu bytes/pixel, total %zu bytes",
    s_width, s_height, bytes_per_pixel, s_buffer_size);

  // Allocate with 64-byte alignment for PPA hardware acceleration
  // Use internal RAM for best performance, fall back to PSRAM for larger buffers
  s_shared_buffer = heap_caps_aligned_alloc(64, s_buffer_size, 
    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_shared_buffer) {
    // Internal RAM insufficient - use PSRAM (normal for 240x240 displays)
    ESP_LOGI(TAG, "Using PSRAM for canvas buffer (internal RAM insufficient)");
    s_shared_buffer = heap_caps_aligned_alloc(64, s_buffer_size, 
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }

  if (!s_shared_buffer) {
    ESP_LOGE(TAG, "Failed to allocate shared canvas buffer!");
    s_buffer_size = 0;
    s_width = 0;
    s_height = 0;
    return false;
  }

  // Initialize to black
  memset(s_shared_buffer, 0, s_buffer_size);

  ESP_LOGI(TAG, "Shared canvas buffer allocated: %zu KB at %p (64-byte aligned)",
    s_buffer_size / 1024, s_shared_buffer);

  return true;
}

void *shared_canvas_buffer_get(void) {
  return s_shared_buffer;
}

size_t shared_canvas_buffer_get_size(void) {
  return s_buffer_size;
}

uint16_t shared_canvas_buffer_get_width(void) {
  return s_width;
}

uint16_t shared_canvas_buffer_get_height(void) {
  return s_height;
}

lv_color_format_t shared_canvas_buffer_get_format(void) {
  return s_color_format;
}

bool shared_canvas_buffer_is_valid(void) {
  return s_shared_buffer != NULL && s_buffer_size > 0;
}

void shared_canvas_buffer_clear(void) {
  if (s_shared_buffer && s_buffer_size > 0) {
    memset(s_shared_buffer, 0, s_buffer_size);
  }
}
