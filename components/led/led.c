#include "io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "task_priorities.h"
#include "app_settings.h"

#define TAG "led"
#define LED_ENABLED_KEY "led_enabled"

static TaskHandle_t task_handle = NULL;
static bool led_enabled = true;

void flash_led(uint32_t duration) {
  if (!led_enabled) return;
  gpio_set_level(PIN_LED, 1);
  vTaskDelay(pdMS_TO_TICKS(duration));
  gpio_set_level(PIN_LED, 0);
}

void led_task(void *pvParameters) {
  while (1) {
    int off_duration = 30000 + (esp_random() % 90000);
    int burst_count = 1 + (esp_random() % 5);
    
    vTaskDelay(pdMS_TO_TICKS(off_duration));

    for (int i = 0; i < burst_count; i++) {
      int on_duration = 50 + (esp_random() % 250);
      flash_led(on_duration);

      if (i < burst_count - 1) {
        int inter_burst_off = 50 + (esp_random() % 100);
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

  bool saved_enabled;
  esp_err_t ret = app_settings_load_bool(LED_ENABLED_KEY, &saved_enabled);
  if (ret == ESP_OK) {
    led_enabled = saved_enabled;
  } else {
    led_enabled = true;
    app_settings_save_bool(LED_ENABLED_KEY, true);
  }

  ESP_LOGI(TAG, "UV LED initialized, enabled: %s", led_enabled ? "true" : "false");
}

void flicker_start(void) {
  if (task_handle != NULL) {
    vTaskResume(task_handle);
    ESP_LOGI(TAG, "Flicker task resumed");
  } else {
    xTaskCreate(led_task, "flicker", 2048, NULL, TASK_PRIORITY_LED, &task_handle);
    ESP_LOGI(TAG, "Flicker task started");
  }
}

void flicker_stop(void) {
  vTaskSuspend(task_handle);
  gpio_set_level(PIN_LED, 0);
  ESP_LOGI(TAG, "Flicker task suspended");
}

void led_set_enabled(bool enabled) {
  led_enabled = enabled;
  esp_err_t ret = app_settings_save_bool(LED_ENABLED_KEY, enabled);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save LED enabled state: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "LED enabled state set to: %s", enabled ? "true" : "false");
  }
}

bool led_get_enabled(void) {
  return led_enabled;
}