#include "range_debug.h"
#include "cv.h"
#include "input_mode.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define TAG "RANGE_DEBUG"

// Button GPIO assignments
#define BTN_BIPOLAR_10V  GPIO_NUM_11
#define BTN_10V          GPIO_NUM_12
#define BTN_BIPOLAR_5V   GPIO_NUM_13
#define BTN_5V           GPIO_NUM_14
#define BTN_3V3          GPIO_NUM_15

static QueueHandle_t s_cv_range_queue = NULL;

// ISR handlers for CV range buttons - defer work to task context
static void IRAM_ATTR cv_range_button_isr(void* arg) {
  cv_range_t range = (cv_range_t)(uintptr_t)arg;
  BaseType_t higher_prio_woken = pdFALSE;
  xQueueSendFromISR(s_cv_range_queue, &range, &higher_prio_woken);
  if (higher_prio_woken) portYIELD_FROM_ISR();
}

// Task to handle CV range changes
static void cv_range_task(void* arg) {
  cv_range_t range;
  while (1) {
    if (xQueueReceive(s_cv_range_queue, &range, portMAX_DELAY)) {
      ESP_LOGI(TAG, "Button pressed, switching to CV range %d", range);
      cv_set_range(range);
    }
  }
}

esp_err_t range_debug_init(void) {
  ESP_LOGI(TAG, "Initializing CV range debug buttons");
  
  // Create queue and task for CV range button handling
  s_cv_range_queue = xQueueCreate(5, sizeof(cv_range_t));
  if (s_cv_range_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create CV range queue");
    return ESP_FAIL;
  }
  
  BaseType_t ret = xTaskCreate(cv_range_task, "cv_range", 4096, NULL, 5, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create CV range task");
    vQueueDelete(s_cv_range_queue);
    s_cv_range_queue = NULL;
    return ESP_FAIL;
  }

  // Configure CV range test buttons
  gpio_config_t io = {
    .pin_bit_mask = (1ULL << BTN_BIPOLAR_10V) | (1ULL << BTN_10V) | 
                    (1ULL << BTN_BIPOLAR_5V) | (1ULL << BTN_5V) | 
                    (1ULL << BTN_3V3),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = 1,
    .pull_down_en = 0,
    .intr_type = GPIO_INTR_NEGEDGE,
    .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE,
  };
  gpio_config(&io);

  // Install GPIO ISR service
  gpio_install_isr_service(0);

  // Hook up ISR handlers - pass CV range enum as argument
  gpio_isr_handler_add(BTN_BIPOLAR_10V, cv_range_button_isr, (void*)(uintptr_t)CV_RANGE_BIPOLAR_10V);
  gpio_isr_handler_add(BTN_10V, cv_range_button_isr, (void*)(uintptr_t)CV_RANGE_10V);
  gpio_isr_handler_add(BTN_BIPOLAR_5V, cv_range_button_isr, (void*)(uintptr_t)CV_RANGE_BIPOLAR_5V);
  gpio_isr_handler_add(BTN_5V, cv_range_button_isr, (void*)(uintptr_t)CV_RANGE_5V);
  gpio_isr_handler_add(BTN_3V3, cv_range_button_isr, (void*)(uintptr_t)CV_RANGE_3V3);

  ESP_LOGI(TAG, "CV range debug buttons configured on GPIO 11-15");
  
  return ESP_OK;
}

