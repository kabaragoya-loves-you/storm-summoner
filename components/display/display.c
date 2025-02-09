#include "lvgl.h"
#include "ssd1327_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LV_BYTES_PER_PIXEL 2
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   128
#define BUFFER_SIZE     (SCREEN_WIDTH * SCREEN_HEIGHT * LV_BYTES_PER_PIXEL)

#define TAG "display"

uint32_t esp_tick_cb(void);
void lvgl_task(void *pvParameter);

void lvgl_setup(void) {
  lv_init();
  ssd1327_init();

  lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(display, ssd1327_flush);
  lv_tick_set_cb(esp_tick_cb);

  /* Allocate the LVGL draw buffer from heap (DMA-capable memory) */
  uint8_t *buf1 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
  uint8_t *buf2 = (uint8_t *)heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA);
  if(!buf1) {
    ESP_LOGE(TAG, "Failed to allocate LVGL buffer. Cannot continue.");
    return;
  }

  lv_display_set_buffers(display, buf1, buf2, BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_FULL);

  xTaskCreate(&lvgl_task, "lvgl_task", 4096, NULL, 5, NULL);
}

void lvgl_task(void *pvParameter) {
  (void) pvParameter;
  while (1) {
    lv_task_handler();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

uint32_t esp_tick_cb(void) {
  return esp_timer_get_time() / 1000;
}
