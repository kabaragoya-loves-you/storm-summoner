#include "performance.h"

#if ENABLE_PERFORMANCE_MONITORING

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "display.h"
#include "ssd1327_driver.h"
#include "task_priorities.h"

static const char *TAG = "PERF";

// Store current display mode
static int current_display_mode = DISPLAY_OPTIMIZATION_MODE;

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
  ESP_LOGI(TAG, "Display Optimization Mode: %d", current_display_mode);
  ESP_LOGI(TAG, "LVGL version: %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  ESP_LOGI(TAG, "Screen: 128x128, Circular mask radius: 64px");
  ESP_LOGI(TAG, "Visible pixels: 12,929 of 16,384 (78.9%%)");
  
  // Log DMA status
  #if ENABLE_SPI_DMA
    ESP_LOGI(TAG, "SPI DMA: ENABLED (23 MHz)");
  #else
    ESP_LOGI(TAG, "SPI DMA: DISABLED (20 MHz)");
  #endif
  
  // Log display configuration
  lv_display_t *disp = lv_display_get_default();
  if (disp != NULL) {
    ESP_LOGI(TAG, "Display resolution: %ldx%ld", 
             (long)lv_display_get_horizontal_resolution(disp),
             (long)lv_display_get_vertical_resolution(disp));
  }
  
  // Log mode-specific information
  switch(current_display_mode) {
    case 0:
      ESP_LOGI(TAG, "Mode 0: Baseline - Full screen single buffer");
      break;
    case 1:
      ESP_LOGI(TAG, "Mode 1: Dynamic visibility check");
      break;
    case 2:
      ESP_LOGI(TAG, "Mode 2: Pre-calculated coordinate map");
      break;
    case 3:
      ESP_LOGI(TAG, "Mode 3: LVGL callback wrapper");
      break;
    case 4:
      ESP_LOGI(TAG, "Mode 4: Sparse buffer analysis (demonstration)");
      ESP_LOGI(TAG, "This mode shows compression potential but doesn't modify buffers");
      break;
  }
  
  #if ENABLE_CONTINUOUS_ANIMATION_TEST
  // Create a simple animated object to force continuous refreshes
  lv_obj_t *test_obj = lv_obj_create(lv_screen_active());
  lv_obj_set_size(test_obj, 50, 50);
  lv_obj_center(test_obj);
  
  // Create animation that moves the object continuously
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, test_obj);
  lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_x);
  lv_anim_set_values(&anim, 20, 108);
  lv_anim_set_time(&anim, 2000);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_playback_time(&anim, 2000);
  lv_anim_start(&anim);
  
  ESP_LOGI(TAG, "Continuous animation test ENABLED - expect steady FPS");
  #endif
  
  while (1) {
    // Wait for the next cycle
    vTaskDelayUntil(&last_wake_time, period_ticks);
    
    ESP_LOGI(TAG, "=== PERFORMANCE STATS (Mode %d) ===", current_display_mode);
    
    // Get LVGL performance stats if available
    #if LV_USE_PERF_MONITOR && LV_USE_PERF_MONITOR_LOG_MODE
      ESP_LOGI(TAG, "LVGL perf monitor is logging internally");
      ESP_LOGI(TAG, "Look for 'sysmon:' lines - FPS averaged over 1000ms windows");
      ESP_LOGI(TAG, "Changed from default 300ms to reduce 0 FPS readings with intermittent animations");
      ESP_LOGI(TAG, "With 1-second averaging, you should see more consistent FPS values");
    #else
      ESP_LOGI(TAG, "Enable LV_USE_PERF_MONITOR_LOG_MODE for detailed stats");
    #endif
    
    // Track LVGL activity
    lv_display_t *disp = lv_display_get_default();
    if (disp != NULL) {
      // Get display inactivity time
      uint32_t inactive_time = lv_display_get_inactive_time(disp);
      ESP_LOGI(TAG, "Display inactive time: %lu ms", inactive_time);
      
      // Get refresh timer info
      lv_timer_t *refr_timer = lv_display_get_refr_timer(disp);
      if (refr_timer != NULL) {
        ESP_LOGI(TAG, "Refresh timer exists");
      }
    }
    
    // Get heap info
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Largest free block: %lu bytes", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
  }
}

void performance_init(void) {
  BaseType_t ret = xTaskCreate(performance_monitor_task, "perf_mon", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create performance monitor task");
  } else {
    ESP_LOGI(TAG, "Performance monitor task created");
  }
}

int performance_get_display_mode(void) {
  return current_display_mode;
}

#endif // ENABLE_PERFORMANCE_MONITORING 