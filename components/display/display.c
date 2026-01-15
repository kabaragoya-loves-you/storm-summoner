#include "lvgl.h"
#include "display_driver.h"
#include "st7789v3_driver.h"
#include "lvgl_stream.h"
#if ENABLE_PERFORMANCE_MONITORING
#include "../lvgl/src/others/sysmon/lv_sysmon.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "task_priorities.h"
#include <string.h>
#include "esp_task_wdt.h"

#define TAG "display"

#if LV_USE_LOG
// Custom log callback to redirect LVGL logs to ESP_LOG
static void lvgl_log_cb(lv_log_level_t level, const char * buf) {
  // Always show sysmon performance logs regardless of level
  if (strstr(buf, "sysmon:") != NULL) {
    ESP_LOGI("LVGL", "%s", buf);
    return;
  }
  
  switch(level) {
    case LV_LOG_LEVEL_ERROR:
      ESP_LOGE("LVGL", "%s", buf);
      break;
    case LV_LOG_LEVEL_WARN:
      ESP_LOGW("LVGL", "%s", buf);
      break;
    case LV_LOG_LEVEL_INFO:
      ESP_LOGI("LVGL", "%s", buf);
      break;
    case LV_LOG_LEVEL_TRACE:
      ESP_LOGD("LVGL", "%s", buf);
      break;
    default:
      ESP_LOGI("LVGL", "%s", buf);
      break;
  }
}
#endif

uint32_t esp_tick_cb(void);
void lvgl_task(void *pvParameter);

// Calculate buffer size for ST7789V3 (240x240 RGB565)
// Use 1/10 of screen = 240 * 24 * 2 = 11,520 bytes per buffer
static size_t calculate_buffer_size(uint16_t width, uint16_t height, lv_color_format_t cf) {
  size_t bytes_per_pixel = lv_color_format_get_size(cf);
  return width * (height / 10) * bytes_per_pixel;
}

void display_init(void) {
  display_driver_select();
  
  const display_driver_t *driver = display_driver_get();
  if (!driver) {
    ESP_LOGE(TAG, "No display driver selected!");
    return;
  }
  
  uint16_t screen_width = display_get_width();
  uint16_t screen_height = display_get_height();
  lv_color_format_t color_format = display_get_color_format();
  size_t bytes_per_pixel = lv_color_format_get_size(color_format);
  size_t buffer_size = calculate_buffer_size(screen_width, screen_height, color_format);

  ESP_LOGI(TAG, "Display: %s (%dx%d, %d bpp)", 
           driver->name, screen_width, screen_height, (int)(bytes_per_pixel * 8));
  ESP_LOGI(TAG, "Free heap before display init: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Free PSRAM: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ESP_LOGI(TAG, "Largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Buffer size needed: %zu bytes each", buffer_size);

  // LVGL will allocate its heap from PSRAM via LV_MEM_POOL_ALLOC
  lv_init();
  
#if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
#endif
  
  // Initialize the display driver
  driver->init();

  lv_display_t *display = lv_display_create(screen_width, screen_height);
  if (!display) {
    ESP_LOGE(TAG, "Failed to create LVGL display. Cannot continue.");
    return;
  }
  
  lv_display_set_color_format(display, color_format);
  lv_display_set_flush_cb(display, driver->flush);
  lv_tick_set_cb(esp_tick_cb);

  ESP_LOGI(TAG, "Temporarily disabling task watchdog for buffer allocation");
  esp_task_wdt_deinit();

  // Double buffering with partial mode for memory efficiency
  ESP_LOGI(TAG, "ST7789V3: Using double buffering with partial mode (%zu bytes each)", buffer_size);
  uint8_t *buf1 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
  uint8_t *buf2 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
  if (!buf1 || !buf2) {
    ESP_LOGE(TAG, "Failed to allocate LVGL buffers. Cannot continue.");
    if (buf1) heap_caps_free(buf1);
    if (buf2) heap_caps_free(buf2);
    return;
  }
  lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

  ESP_LOGI(TAG, "Task watchdog re-enabled");

#if ENABLE_PERFORMANCE_MONITORING
  ESP_LOGI(TAG, "Performance monitoring temporarily disabled to debug kernel panic");
#endif

  ESP_LOGI(TAG, "Display initialization complete - call display_start() to begin rendering");
}

void display_start(void) {
  BaseType_t task_result = xTaskCreate(&lvgl_task, "lvgl", 8192, NULL, TASK_PRIORITY_DISPLAY, NULL);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    return;
  }
  ESP_LOGI(TAG, "LVGL rendering started");
}

void lvgl_task(void *pvParameter) {
  (void) pvParameter;
  while (1) {
    // Check for stream sync request - invalidate entire display to force full redraw
    // Also check for deferred sync after frame drops have settled
    if (lvgl_stream_sync_pending() || lvgl_stream_needs_deferred_sync()) {
      lv_obj_t *screen = lv_screen_active();
      if (screen) {
        lv_obj_invalidate(screen);
        lvgl_stream_sync_done();
      }
    }
    
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

uint32_t esp_tick_cb(void) {
  return esp_timer_get_time() / 1000;
}
