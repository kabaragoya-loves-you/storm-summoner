#include "lvgl.h"
#include "ssd1327_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "task_priorities.h"
#if DISPLAY_OPTIMIZATION_MODE == 2
#include "coordinate_map.h"
#endif
#if SHOW_PERF_MONITOR
#include "../lvgl/src/others/sysmon/lv_sysmon.h"
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
  // Mode 2: Coordinate Map with Sparse Double Buffering
  #define BUFFER_SIZE (VISIBLE_PIXEL_COUNT * LV_BYTES_PER_PIXEL)
#endif

#define TAG "display"

uint32_t esp_tick_cb(void);
void lvgl_task(void *pvParameter);

void display_init(void) {
  ESP_LOGI(TAG, "Free heap before display init: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  ESP_LOGI(TAG, "Buffer size needed: %d bytes each", BUFFER_SIZE);

  lv_init();
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
  // Modes 1 and 2 use double buffering
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
    ESP_LOGI(TAG, "Using Coordinate Map. Render Mode: DIRECT");
    lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
  #endif
#endif

  ESP_LOGI(TAG, "Task watchdog re-enabled");

#if SHOW_PERF_MONITOR
    lv_sysmon_create(display);
#endif

  BaseType_t task_result = xTaskCreate(&lvgl_task, "lvgl", 4096, NULL, TASK_PRIORITY_DISPLAY, NULL);
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