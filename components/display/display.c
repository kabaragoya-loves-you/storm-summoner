#include "lvgl.h"
#include "display_driver.h"
#include "ssd1327_driver.h"
#include "gc9a01a_driver.h"
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
#if DISPLAY_OPTIMIZATION_MODE == 2
#include "coordinate_map.h"
#endif
#if DISPLAY_OPTIMIZATION_MODE == 3
#include "circular_display.h"
#endif
#if DISPLAY_OPTIMIZATION_MODE == 4
#include "sparse_buffer.h"
#endif
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

// Calculate buffer size based on display and color format
static size_t calculate_buffer_size(uint16_t width, uint16_t height, lv_color_format_t cf, display_type_t disp_type) {
  size_t bytes_per_pixel = lv_color_format_get_size(cf);
  
  // For GC9A01A (240x240 color), always use partial buffers for memory efficiency
  // Full buffer would be 172KB which wastes RAM
  if (disp_type == DISPLAY_TYPE_GC9A01A) {
    // 1/10 of screen = 240 * 24 * 3 = 17,280 bytes per buffer
    return width * (height / 10) * bytes_per_pixel;
  }
  
  // For SSD1327, use compile-time mode
#if DISPLAY_OPTIMIZATION_MODE == 0
  // Mode 0: Full-screen single buffer
  return width * height * bytes_per_pixel;
#elif DISPLAY_OPTIMIZATION_MODE == 4
  // Mode 4: Smaller buffers for sparse compression
  return width * (height / 16) * bytes_per_pixel;
#else
  // Modes 1, 2, 3, 5: Partial buffers (1/8 screen)
  return width * (height / 8) * bytes_per_pixel;
#endif
}

void display_init(void) {
  // Select the appropriate driver based on hardware revision
  // NOTE: revision_init() must be called before display_init()
  display_driver_select();
  
  const display_driver_t *driver = display_driver_get();
  if (!driver) {
    ESP_LOGE(TAG, "No display driver selected!");
    return;
  }
  
  uint16_t screen_width = driver->width;
  uint16_t screen_height = driver->height;
  lv_color_format_t color_format = driver->color_format;
  display_type_t disp_type = display_driver_get_type();
  size_t bytes_per_pixel = lv_color_format_get_size(color_format);
  size_t buffer_size = calculate_buffer_size(screen_width, screen_height, color_format, disp_type);

  ESP_LOGI(TAG, "Display: %s (%dx%d, %d bpp)", 
           driver->name, screen_width, screen_height, (int)(bytes_per_pixel * 8));
  ESP_LOGI(TAG, "Free heap before display init: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Free PSRAM: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ESP_LOGI(TAG, "Largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Buffer size needed: %zu bytes each", buffer_size);

  // LVGL will allocate its heap from PSRAM via LV_MEM_POOL_ALLOC
  lv_init();
  
  #if LV_USE_LOG
    // Register custom log callback to redirect to ESP_LOG
    lv_log_register_print_cb(lvgl_log_cb);
  #endif
  
  // Initialize the selected display driver
  driver->init();

  lv_display_t *display = lv_display_create(screen_width, screen_height);
  if (!display) {
    ESP_LOGE(TAG, "Failed to create LVGL display. Cannot continue.");
    return;
  }
  
  // Set the color format for the display
  lv_display_set_color_format(display, color_format);
  
  lv_display_set_flush_cb(display, driver->flush);
  lv_tick_set_cb(esp_tick_cb);

  ESP_LOGI(TAG, "Temporarily disabling task watchdog for buffer allocation");
  esp_task_wdt_deinit();

  // GC9A01A always uses double buffering with partial mode for memory efficiency
  if (disp_type == DISPLAY_TYPE_GC9A01A) {
    ESP_LOGI(TAG, "GC9A01A: Using double buffering with partial mode (%zu bytes each)", buffer_size);
    uint8_t *buf1 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
    uint8_t *buf2 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
      ESP_LOGE(TAG, "Failed to allocate LVGL buffers. Cannot continue.");
      if (buf1) heap_caps_free(buf1);
      if (buf2) heap_caps_free(buf2);
      return;
    }
    lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  } else {
    // SSD1327 uses compile-time DISPLAY_OPTIMIZATION_MODE
#if DISPLAY_OPTIMIZATION_MODE == 0
    ESP_LOGI(TAG, "SSD1327: Using Full-Screen Single Buffer. Render Mode: FULL");
    uint8_t *buf1 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
    if (!buf1) {
      ESP_LOGE(TAG, "Failed to allocate LVGL buffer for Mode 0. Cannot continue.");
      return;
    }
    lv_display_set_buffers(display, buf1, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_FULL);
#else
    // Modes 1, 2, 3, 4, 5 use double buffering
    ESP_LOGI(TAG, "SSD1327: Allocating two buffers of %zu bytes each for double buffering.", buffer_size);
    uint8_t *buf1 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
    uint8_t *buf2 = (uint8_t *)heap_caps_aligned_alloc(64, buffer_size, MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
      ESP_LOGE(TAG, "Failed to allocate LVGL buffers for double buffering. Cannot continue.");
      if (buf1) heap_caps_free(buf1);
      if (buf2) heap_caps_free(buf2);
      return;
    }
  #if DISPLAY_OPTIMIZATION_MODE == 1
      ESP_LOGI(TAG, "Using Dynamic Calculation. Render Mode: PARTIAL");
      lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #elif DISPLAY_OPTIMIZATION_MODE == 2
      ESP_LOGI(TAG, "Using Coordinate Map. Render Mode: PARTIAL");
      lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #elif DISPLAY_OPTIMIZATION_MODE == 3
      ESP_LOGI(TAG, "Using LVGL-Integrated Circular Display. Render Mode: PARTIAL");
      lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
      circular_display_init(display);
  #elif DISPLAY_OPTIMIZATION_MODE == 4
      ESP_LOGI(TAG, "Using Sparse Buffer with Compressed Storage. Render Mode: PARTIAL");
      lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
      sparse_buffer_init(display);
  #elif DISPLAY_OPTIMIZATION_MODE == 5
      ESP_LOGI(TAG, "Using RGB565 with I4 Display Driver Conversion. Render Mode: PARTIAL");
      lv_display_set_buffers(display, buf1, buf2, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #endif
#endif
  }

  ESP_LOGI(TAG, "Task watchdog re-enabled");

#if ENABLE_PERFORMANCE_MONITORING
    ESP_LOGI(TAG, "Performance monitoring temporarily disabled to debug kernel panic");
    // TODO: Re-enable after display system is stable
    #if 0
    #if LV_USE_SYSMON
        ESP_LOGI(TAG, "Enabling LVGL performance monitoring");
        lv_obj_t *sysmon = lv_sysmon_create(display);
        if (sysmon) {
            ESP_LOGI(TAG, "LVGL system monitor created successfully - FPS/CPU will be logged");
        } else {
            ESP_LOGW(TAG, "Failed to create LVGL system monitor");
        }
    #else
        ESP_LOGW(TAG, "Performance monitoring requested but LVGL sysmon module not available");
    #endif
    #endif
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
    lv_timer_handler();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

uint32_t esp_tick_cb(void) {
  return esp_timer_get_time() / 1000;
}
