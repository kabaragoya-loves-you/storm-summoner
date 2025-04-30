#include "midi_out.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "task_priorities.h"

#define TAG "MIDI_OUT"
#define MIDI_QUEUE_LENGTH   50
#define MIDI_QUEUE_ITEM_SIZE sizeof(midi_out_job_t *)
#define MIDI_MIN_INTERVAL   pdMS_TO_TICKS(10)

static QueueHandle_t   midi_out_queue  = NULL;
static SemaphoreHandle_t midi_out_mutex = NULL;
static TickType_t        last_send_tick  = 0;
static midi_transmit_mode_t current_mode = MIDI_TRANSMIT_BOTH;

static void midi_out_task(void *pvParameters);

void midi_out_init(void) {
  if (midi_out_queue != NULL) {
    ESP_LOGW(TAG, "MIDI queue already initialized");
    return;
  }

  uart_config_t uart_config = {
    .baud_rate = 31250,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, MIDI_TXD, MIDI_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
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

  midi_out_queue = xQueueCreate(MIDI_QUEUE_LENGTH, MIDI_QUEUE_ITEM_SIZE);
  if (midi_out_queue == NULL) {
    ESP_LOGE(TAG, "Failed to create MIDI queue");
    return;
  }

  midi_out_mutex = xSemaphoreCreateMutex();
  if (midi_out_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create MIDI mutex");
    vQueueDelete(midi_out_queue);
    midi_out_queue = NULL;
    return;
  }

  BaseType_t ret = xTaskCreate(midi_out_task, "midi_out", 4096, NULL, TASK_PRIORITY_MIDI_OUT, NULL);
  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create MIDI task");
    vQueueDelete(midi_out_queue);
    midi_out_queue = NULL;
    vSemaphoreDelete(midi_out_mutex);
    midi_out_mutex = NULL;
    return;
  }

  ESP_LOGI(TAG, "MIDI OUT initialized successfully");
}

void midi_send_message(const uint8_t *stream, size_t len) {
  if (midi_out_queue == NULL) {
    ESP_LOGE(TAG, "MIDI queue not initialized! Call midi_out_init() first");
    return;
  }

  midi_out_job_t *job = malloc(sizeof(midi_out_job_t));
  if (!job) {
    ESP_LOGE(TAG, "Failed to allocate job structure");
    return;
  }

  job->data = malloc(len);
  if (!job->data) {
    ESP_LOGE(TAG, "Failed to allocate job data");
    free(job);
    return;
  }

  memcpy(job->data, stream, len);
  job->len = len;

  // Try to send immediately, overwriting any pending message
  if (xQueueSendToFront(midi_out_queue, &job, 0) != pdPASS) {
    // If queue is full, remove the oldest message and try again
    midi_out_job_t *old_job;
    if (xQueueReceive(midi_out_queue, &old_job, 0) == pdPASS) {
      free(old_job->data);
      free(old_job);
    }
    // Now try to send again
    if (xQueueSendToFront(midi_out_queue, &job, 0) != pdPASS) {
      ESP_LOGW(TAG, "Failed to send MIDI message - queue full even after clearing");
      free(job->data);
      free(job);
    }
  }
}

void midi_clear_queue(void) {
  if (midi_out_queue == NULL) return;
  
  midi_out_job_t *job;
  while (xQueueReceive(midi_out_queue, &job, 0) == pdPASS) {
    free(job->data);
    free(job);
  }
}

void midi_set_transmit_mode(midi_transmit_mode_t mode) {
  if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
    current_mode = mode;
    xSemaphoreGive(midi_out_mutex);
  }
}

static void midi_out_task(void *pvParameters) {
  midi_out_job_t *job;
  for (;;) {
    if (xQueueReceive(midi_out_queue, &job, portMAX_DELAY) == pdPASS) {
      TickType_t current_tick = xTaskGetTickCount();
      if (last_send_tick != 0) {
        TickType_t elapsed = current_tick - last_send_tick;
        if (elapsed < MIDI_MIN_INTERVAL) {
          vTaskDelay(MIDI_MIN_INTERVAL - elapsed);
        }
      }

      if (xSemaphoreTake(midi_out_mutex, portMAX_DELAY) == pdPASS) {
        switch (current_mode) {
          case MIDI_TRANSMIT_BOTH:
            gpio_set_level(PIN_POLARITY, TYPE_A);
            uart_write_bytes(UART_NUM_1, job->data, job->len);
            vTaskDelay(MIDI_MIN_INTERVAL);
            gpio_set_level(PIN_POLARITY, TYPE_B);
            uart_write_bytes(UART_NUM_1, job->data, job->len);
            break;
            
          case MIDI_TRANSMIT_TYPE_A:
            gpio_set_level(PIN_POLARITY, TYPE_A);
            uart_write_bytes(UART_NUM_1, job->data, job->len);
            break;
            
          case MIDI_TRANSMIT_TYPE_B:
            gpio_set_level(PIN_POLARITY, TYPE_B);
            uart_write_bytes(UART_NUM_1, job->data, job->len);
            break;
        }
        last_send_tick = xTaskGetTickCount();
        xSemaphoreGive(midi_out_mutex);
      }
      free(job->data);
      free(job);
    }
  }
}
