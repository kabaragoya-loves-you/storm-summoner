#include "performance.h"

#if ENABLE_PERFORMANCE_MONITORING

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include "display.h"
#include "display_driver.h"
#include "task_priorities.h"

static const char *TAG = "PERF";

// Custom log callback to route LVGL logs through ESP-IDF logging
static void lvgl_log_cb(lv_log_level_t level, const char *buf) {
  // Filter out noisy idle percentage warnings
  if (level == LV_LOG_LEVEL_WARN && strstr(buf, "lv_os_get_idle_percent") != NULL) {
    return;  // Suppress this specific warning
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
    case LV_LOG_LEVEL_USER:
    case LV_LOG_LEVEL_TRACE:
    default:
      ESP_LOGD("LVGL", "%s", buf);
      break;
  }
}

void performance_init(void) {
#if LV_USE_LOG
  lv_log_register_print_cb(lvgl_log_cb);
  ESP_LOGI(TAG, "LVGL logging enabled and redirected to ESP-IDF");
#endif
  
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
  
  // No ongoing task needed - use 'perf' console command or on-screen FPS display
}

#endif // ENABLE_PERFORMANCE_MONITORING
