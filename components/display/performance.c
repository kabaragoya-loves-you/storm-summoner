#include "performance.h"

#if ENABLE_PERFORMANCE_MONITORING

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "display.h"
#include "display_driver.h"
#include "task_priorities.h"

static const char *TAG = "PERF";

// Custom log callback to route LVGL logs through ESP-IDF logging
static void lvgl_log_cb(lv_log_level_t level, const char *buf) {
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
    case LV_LOG_LEVEL_USER:
    case LV_LOG_LEVEL_TRACE:
    default:
      ESP_LOGD("LVGL", "%s", buf);
      break;
  }
}

static void performance_monitor_task(void *pvParameters) {
#if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
  ESP_LOGI(TAG, "LVGL logging enabled and redirected to ESP-IDF");
#endif
  
  TickType_t last_wake_time = xTaskGetTickCount();
  const TickType_t period_ticks = pdMS_TO_TICKS(5000); // Log every 5 seconds
  
  // Log initial configuration
  ESP_LOGI(TAG, "=== DISPLAY PERFORMANCE MONITOR STARTED ===");
  ESP_LOGI(TAG, "Display: GC9A01A 240x240 RGB888 IPS");
  ESP_LOGI(TAG, "LVGL version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  
#if ENABLE_SPI_DMA
  ESP_LOGI(TAG, "SPI DMA: ENABLED");
#else
  ESP_LOGI(TAG, "SPI DMA: DISABLED");
#endif
  
  // Log display configuration
  lv_display_t *disp = lv_display_get_default();
  if (disp != NULL) {
    ESP_LOGI(TAG, "Display resolution: %ldx%ld", 
             (long)lv_display_get_horizontal_resolution(disp),
             (long)lv_display_get_vertical_resolution(disp));
  }
  
  while (1) {
    vTaskDelayUntil(&last_wake_time, period_ticks);

    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
      uint32_t inactive_time = lv_display_get_inactive_time(disp);
      ESP_LOGI(TAG, "Display inactive time: %lu ms", inactive_time);
    }
    
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Largest free block: %lu bytes", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  }
}

void performance_init(void) {
  BaseType_t ret = xTaskCreate(performance_monitor_task, "perf_mon", 3072, NULL, tskIDLE_PRIORITY + 1, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create performance monitor task");
  } else {
    ESP_LOGI(TAG, "Performance monitor task created");
  }
}

#endif // ENABLE_PERFORMANCE_MONITORING
