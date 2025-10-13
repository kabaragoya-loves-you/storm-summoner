#include "touch_debug.h"
#include "touch.h"
#include "task_monitor.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_timer.h"

#define TAG "TOUCH_DEBUG"

// Debug button configuration
#define DEBUG_BUTTON_GPIO GPIO_NUM_35
#define LONG_PRESS_TIME_MS 3000
#define DOUBLE_CLICK_TIME_MS 500

// Debug button state
static int64_t debug_button_press_time = 0;
static int debug_button_click_count = 0;
static TimerHandle_t debug_button_timer = NULL;
static bool debug_button_long_press = false;
static QueueHandle_t debug_button_queue = NULL;

// Debug button action types
typedef enum {
  DEBUG_ACTION_SINGLE_CLICK,
  DEBUG_ACTION_DOUBLE_CLICK,
  DEBUG_ACTION_TRIPLE_CLICK,
  DEBUG_ACTION_QUAD_CLICK,
  DEBUG_ACTION_LONG_PRESS
} debug_action_t;

// Forward declarations
static void debug_button_timer_callback(TimerHandle_t xTimer);
static void debug_button_isr(void* arg);
static void debug_button_task(void* arg);

// Debug button task - handles actions with sufficient stack
static void debug_button_task(void* arg) {
  debug_action_t action;
  
  while (1) {
    if (xQueueReceive(debug_button_queue, &action, portMAX_DELAY) == pdTRUE) {
      switch (action) {
        case DEBUG_ACTION_SINGLE_CLICK:
          ESP_LOGI(TAG, "Single click - Enabling touch debug");
          touch_enable_debug_logging();
          break;
          
        case DEBUG_ACTION_DOUBLE_CLICK:
          ESP_LOGI(TAG, "Double click - Resetting stuck touch pads");
          touch_reset_stuck_pads();
          break;
          
        case DEBUG_ACTION_TRIPLE_CLICK:
          ESP_LOGI(TAG, "Triple click - Showing heap info");
          task_monitor_print_heap_info();
          break;
          
        case DEBUG_ACTION_QUAD_CLICK:
          ESP_LOGI(TAG, "Quad+ click - Showing task report");
          task_monitor_print_report();
          break;
          
        case DEBUG_ACTION_LONG_PRESS:
          ESP_LOGI(TAG, "Long press - Starting touch calibration");
          force_touch_calibration();
          break;
      }
    }
  }
}

// Debug button timer callback - handles click counting
static void debug_button_timer_callback(TimerHandle_t xTimer) {
  debug_action_t action;
  
  // Check for long press flag first
  if (debug_button_long_press) {
    debug_button_long_press = false;
    action = DEBUG_ACTION_LONG_PRESS;
  } else if (debug_button_click_count == 1) {
    action = DEBUG_ACTION_SINGLE_CLICK;
  } else if (debug_button_click_count == 2) {
    action = DEBUG_ACTION_DOUBLE_CLICK;
  } else if (debug_button_click_count == 3) {
    action = DEBUG_ACTION_TRIPLE_CLICK;
  } else if (debug_button_click_count >= 4) {
    action = DEBUG_ACTION_QUAD_CLICK;
  } else {
    debug_button_click_count = 0;
    return;
  }
  
  debug_button_click_count = 0;
  
  // Queue the action for the task to handle
  xQueueSend(debug_button_queue, &action, 0);
}

// Debug button ISR
static void IRAM_ATTR debug_button_isr(void* arg) {
  int level = gpio_get_level(DEBUG_BUTTON_GPIO);
  
  if (level == 0) {  // Button pressed (active low)
    debug_button_press_time = esp_timer_get_time();
  } else {  // Button released
    if (debug_button_press_time > 0) {
      int64_t press_duration = (esp_timer_get_time() - debug_button_press_time) / 1000;  // Convert to ms
      debug_button_press_time = 0;
      
      if (press_duration >= LONG_PRESS_TIME_MS) {
        // Long press - set flag and use timer to handle it safely
        debug_button_long_press = true;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTimerResetFromISR(debug_button_timer, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
      } else {
        // Short press - count clicks
        debug_button_click_count++;
        
        // Reset or start timer
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTimerResetFromISR(debug_button_timer, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
      }
    }
  }
}

// Initialize debug button
esp_err_t touch_debug_init(void) {
  ESP_LOGI(TAG, "Initializing debug button on GPIO%d", DEBUG_BUTTON_GPIO);
  
  // Configure GPIO
  gpio_config_t io_conf = {
    .intr_type = GPIO_INTR_ANYEDGE,
    .mode = GPIO_MODE_INPUT,
    .pin_bit_mask = (1ULL << DEBUG_BUTTON_GPIO),
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .pull_up_en = GPIO_PULLUP_ENABLE  // Internal pull-up since button connects to ground
  };
  gpio_config(&io_conf);
  
  // Create queue for debug actions
  debug_button_queue = xQueueCreate(5, sizeof(debug_action_t));
  if (debug_button_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create debug button queue");
    return ESP_ERR_NO_MEM;
  }
  
  // Create timer for click counting
  debug_button_timer = xTimerCreate("debug_btn", 
                                   pdMS_TO_TICKS(DOUBLE_CLICK_TIME_MS),
                                   pdFALSE,  // Don't auto-reload
                                   NULL,
                                   debug_button_timer_callback);
  if (debug_button_timer == NULL) {
    ESP_LOGE(TAG, "Failed to create debug button timer");
    vQueueDelete(debug_button_queue);
    return ESP_ERR_NO_MEM;
  }
  
  // Create task to handle debug actions
  if (xTaskCreate(debug_button_task, "debug_btn_task", 4096, NULL, 5, NULL) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create debug button task");
    xTimerDelete(debug_button_timer, 0);
    vQueueDelete(debug_button_queue);
    return ESP_ERR_NO_MEM;
  }
  
  // Install ISR
  gpio_isr_handler_add(DEBUG_BUTTON_GPIO, debug_button_isr, NULL);
  
  ESP_LOGI(TAG, "Debug button ready:");
  ESP_LOGI(TAG, "  - Single click: Touch debug");
  ESP_LOGI(TAG, "  - Double click: Reset stuck touch pads");
  ESP_LOGI(TAG, "  - Triple click: Show heap info");
  ESP_LOGI(TAG, "  - Quad click: Show task report");
  ESP_LOGI(TAG, "  - Long press (3s): Force touch calibration");
  
  return ESP_OK;
}
