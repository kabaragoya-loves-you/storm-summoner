#include "lvgl.h"
#include "ssd1327_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "task_priorities.h"

#define LV_BYTES_PER_PIXEL 2
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   128
#define BUFFER_SIZE     (SCREEN_WIDTH * SCREEN_HEIGHT * LV_BYTES_PER_PIXEL)

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

  /* Allocate the LVGL draw buffers from heap (DMA-capable memory) */
  /* Try to allocate both buffers first to check memory availability */
  
  // First attempt: try to allocate a single large block for both buffers
  uint8_t *combined_buffer = (uint8_t *)heap_caps_malloc(BUFFER_SIZE * 2, MALLOC_CAP_DMA);
  uint8_t *buf1, *buf2 = NULL;
  
  if (combined_buffer) {
    ESP_LOGI(TAG, "Allocated combined DMA buffer (%d bytes) for dual buffer mode", BUFFER_SIZE * 2);
    buf1 = combined_buffer;
    buf2 = combined_buffer + BUFFER_SIZE;
    ESP_LOGI(TAG, "Buffer 1 at %p, Buffer 2 at %p", (void*)buf1, (void*)buf2);
    ESP_LOGI(TAG, "Dual buffer mode enabled (contiguous DMA allocation)");
  } else {
    ESP_LOGW(TAG, "Failed to allocate contiguous DMA buffers, trying separate allocation");
    
    // Fallback: allocate buffers separately
    buf1 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
    if(!buf1) {
      ESP_LOGE(TAG, "Failed to allocate LVGL buffer 1 (%d bytes). Cannot continue.", BUFFER_SIZE);
      ESP_LOGE(TAG, "Available DMA memory: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
      return;
    }
    ESP_LOGI(TAG, "Buffer 1 allocated successfully at %p", (void*)buf1);
    
    buf2 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
    if(!buf2) {
      ESP_LOGW(TAG, "Failed to allocate LVGL buffer 2 (%d bytes). Using single buffer mode.", BUFFER_SIZE);
      ESP_LOGW(TAG, "Available DMA memory after buf1: %zu bytes", heap_caps_get_free_size(MALLOC_CAP_DMA));
      ESP_LOGW(TAG, "Largest free block: %zu bytes", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    } else {
      ESP_LOGI(TAG, "Buffer 2 allocated successfully at %p", (void*)buf2);
      ESP_LOGI(TAG, "Dual buffer mode enabled (separate DMA allocation)");
    }
  }

  lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_FULL);

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
