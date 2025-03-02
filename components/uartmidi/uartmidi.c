#include "uartmidi.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define TAG "UARTMIDI"
#define UARTMIDI_QUEUE_LENGTH   10
#define UARTMIDI_QUEUE_ITEM_SIZE sizeof(uartmidi_job_t)
#define UARTMIDI_MIN_INTERVAL   pdMS_TO_TICKS(10)

static QueueHandle_t   uartmidi_queue  = NULL;
static SemaphoreHandle_t uartmidi_mutex = NULL;
static TickType_t        last_send_tick  = 0;

static void uartmidi_task(void *pvParameters);

void uartmidi_init(void) {
  uart_config_t uart_config = {
    .baud_rate = 31250,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UARTMIDI_TXD, UARTMIDI_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0));

  uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_TXD_INV);

  gpio_config_t io_conf2 = {
    .pin_bit_mask = (1ULL << PIN_POLARITY),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf2);

  uartmidi_queue  = xQueueCreate(UARTMIDI_QUEUE_LENGTH, UARTMIDI_QUEUE_ITEM_SIZE);
  uartmidi_mutex  = xSemaphoreCreateMutex();

  xTaskCreate(uartmidi_task, "uartmidi_task", 4096, NULL, 5, NULL);
  
  ESP_LOGI(TAG, "Serial MIDI initialized");
}

void uartmidi_send_message(const uint8_t *stream, size_t len) {
  uartmidi_job_t job;
  job.data = malloc(len);

  memcpy(job.data, stream, len);
  job.len = len;

  xQueueSend(uartmidi_queue, &job, (TickType_t) 10);
}

static void uartmidi_task(void *pvParameters) {
  uartmidi_job_t job;
  for (;;) {
    if (xQueueReceive(uartmidi_queue, &job, portMAX_DELAY) == pdPASS) {
      if (xSemaphoreTake(uartmidi_mutex, portMAX_DELAY) == pdPASS) {
        TickType_t current_tick = xTaskGetTickCount();
        if (last_send_tick != 0) {
          TickType_t elapsed = current_tick - last_send_tick;
          if (elapsed < UARTMIDI_MIN_INTERVAL) {
            ESP_LOGW(TAG, "waiting for %"PRIu32" tick(s)", UARTMIDI_MIN_INTERVAL - elapsed);
            vTaskDelay(UARTMIDI_MIN_INTERVAL - elapsed);
          }
        }

        ESP_LOG_BUFFER_HEX(TAG, job.data, job.len);
      
        gpio_set_level(PIN_POLARITY, TYPE_A);
        uart_write_bytes(UART_NUM_1, job.data, job.len);
      
        vTaskDelay(UARTMIDI_MIN_INTERVAL);
      
        gpio_set_level(PIN_POLARITY, TYPE_B);
        uart_write_bytes(UART_NUM_1, job.data, job.len);

        last_send_tick = xTaskGetTickCount();
        xSemaphoreGive(uartmidi_mutex);
      }
      free(job.data);
    }
  }
}
