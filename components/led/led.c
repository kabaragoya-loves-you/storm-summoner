#include "io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "task_priorities.h"

#define TAG "led"

static TaskHandle_t task_handle = NULL;

void led_task(void *pvParameters) {
  while (1) {
    int off_duration = 30000 + (esp_random() % 90000);
    gpio_set_level(PIN_LED, 0);
    vTaskDelay(pdMS_TO_TICKS(off_duration));

    int burst_count = 1 + (esp_random() % 5);
    for (int i = 0; i < burst_count; i++) {
      int on_duration = 50 + (esp_random() % 250);
      gpio_set_level(PIN_LED, 1);
      vTaskDelay(pdMS_TO_TICKS(on_duration));

      if (i < burst_count - 1) {
        int inter_burst_off = 50 + (esp_random() % 100);
        gpio_set_level(PIN_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(inter_burst_off));
      }
    }
  }
}

void led_init(void) {
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_LED),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);

  ESP_LOGI(TAG, "UV LED initialized");
}

void led_enable(void) {
  if (task_handle != NULL) {
    vTaskResume(task_handle);
    ESP_LOGI(TAG, "UV LED job task resumed");
  } else {
    xTaskCreate(led_task, "led", 2048, NULL, TASK_PRIORITY_LED, &task_handle);
    ESP_LOGI(TAG, "UV LED job task started");
  }
}

void led_disable(void) {
  vTaskSuspend(task_handle);
  gpio_set_level(PIN_LED, 0);
  ESP_LOGI(TAG, "UV LED job task suspended");
}
