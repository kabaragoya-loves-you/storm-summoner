#include "lvgl.h"
#include "ssd1327_driver.h"
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

#define LV_BYTES_PER_PIXEL 2
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 128

#if DISPLAY_OPTIMIZATION_MODE == 0
  // Mode 0: Original Full-Screen Single Buffer
  #define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * LV_BYTES_PER_PIXEL)
#elif DISPLAY_OPTIMIZATION_MODE == 1
  // Mode 1: Dynamic Calculation with Partial Double Buffering
  #define BUFFER_PIXEL_COUNT (SCREEN_WIDTH * (SCREEN_HEIGHT / 8))
  #define BUFFER_SIZE (BUFFER_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
#elif DISPLAY_OPTIMIZATION_MODE == 2
  // Mode 2: Coordinate Map with Partial Double Buffering
  #define BUFFER_PIXEL_COUNT (SCREEN_WIDTH * (SCREEN_HEIGHT / 8))
  #define BUFFER_SIZE (BUFFER_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
#elif DISPLAY_OPTIMIZATION_MODE == 3
  // Mode 3: LVGL-Integrated Circular Display Optimization
  #define BUFFER_PIXEL_COUNT (SCREEN_WIDTH * (SCREEN_HEIGHT / 8))
  #define BUFFER_SIZE (BUFFER_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
#elif DISPLAY_OPTIMIZATION_MODE == 4
  // Mode 4: Sparse Buffer with Compressed Storage
  // Use smaller buffers since we'll compress between operations
  #define BUFFER_PIXEL_COUNT (SCREEN_WIDTH * (SCREEN_HEIGHT / 16))
  #define BUFFER_SIZE (BUFFER_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
  #elif DISPLAY_OPTIMIZATION_MODE == 5
    // Mode 5: RGB565 with I4 Display Driver Conversion (Partial Double Buffering)
    #define BUFFER_PIXEL_COUNT (SCREEN_WIDTH * (SCREEN_HEIGHT / 8))
    #define BUFFER_SIZE (BUFFER_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
#else
  // Fallback - should not happen
  #define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * LV_BYTES_PER_PIXEL)
#endif

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

void display_init(void) {
  ESP_LOGI(TAG, "Free heap before display init: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Buffer size needed: %d bytes each", BUFFER_SIZE);

  lv_init();
  
  #if LV_USE_LOG
    // Register custom log callback to redirect to ESP_LOG
    lv_log_register_print_cb(lvgl_log_cb);
  #endif
  
  ssd1327_init();

  lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  if (!display) {
    ESP_LOGE(TAG, "Failed to create LVGL display. Cannot continue.");
    return;
  }
  
  lv_display_set_flush_cb(display, ssd1327_flush);
  lv_tick_set_cb(esp_tick_cb);

  ESP_LOGI(TAG, "Temporarily disabling task watchdog for buffer allocation");
  esp_task_wdt_deinit();

#if DISPLAY_OPTIMIZATION_MODE == 0
  ESP_LOGI(TAG, "Using Original Full-Screen Single Buffer. Render Mode: FULL");
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
  if (!buf1) {
    ESP_LOGE(TAG, "Failed to allocate LVGL buffer for Mode 0. Cannot continue.");
    return;
  }
  lv_display_set_buffers(display, buf1, NULL, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
#else
  // Modes 1, 2, and 3 use double buffering
  ESP_LOGI(TAG, "Allocating two buffers of %d bytes each for double buffering.", BUFFER_SIZE);
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
  uint8_t *buf2 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
  if (!buf1 || !buf2) {
    ESP_LOGE(TAG, "Failed to allocate LVGL buffers for double buffering. Cannot continue.");
    if(buf1) heap_caps_free(buf1);
    if(buf2) heap_caps_free(buf2);
    return;
  }
  #if DISPLAY_OPTIMIZATION_MODE == 1
    ESP_LOGI(TAG, "Using Dynamic Calculation. Render Mode: PARTIAL");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #elif DISPLAY_OPTIMIZATION_MODE == 2
    ESP_LOGI(TAG, "Using Coordinate Map. Render Mode: PARTIAL");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #elif DISPLAY_OPTIMIZATION_MODE == 3
    ESP_LOGI(TAG, "Using LVGL-Integrated Circular Display. Render Mode: PARTIAL");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // Initialize circular display optimization
    circular_display_init(display);
  #elif DISPLAY_OPTIMIZATION_MODE == 4
    ESP_LOGI(TAG, "Using Sparse Buffer with Compressed Storage. Render Mode: PARTIAL");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // Initialize sparse buffer system
    sparse_buffer_init(display);
  #elif DISPLAY_OPTIMIZATION_MODE == 5
    ESP_LOGI(TAG, "Using RGB565 with I4 Display Driver Conversion. Render Mode: PARTIAL");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
  #endif
#endif

  ESP_LOGI(TAG, "Task watchdog re-enabled");

#if ENABLE_PERFORMANCE_MONITORING
    ESP_LOGI(TAG, "Performance monitoring temporarily disabled to debug kernel panic");
    ESP_LOGI(TAG, "Kernel panic occurs in lv_refr_get_top_obj - investigating display system first");
    // TODO: Re-enable after Mode 5 display system is stable
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
        ESP_LOGW(TAG, "Enable LV_USE_SYSMON = 1 in LVGL configuration for FPS/CPU monitoring");
    #endif
    #endif
#endif

      BaseType_t task_result = xTaskCreate(&lvgl_task, "lvgl", 8192, NULL, TASK_PRIORITY_DISPLAY, NULL);
  if (task_result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create LVGL task");
    return;
  }
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