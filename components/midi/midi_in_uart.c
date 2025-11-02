/**
 * MIDI IN UART Transport
 * 
 * UART-specific MIDI IN implementation.
 * Handles UART reading and cable detection.
 */

#include "midi_in_uart.h"
#include "midi_in_parser.h"
#include "midi_in.h"
#include "input_manager.h"
#include "event_bus.h"
#include "io.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "task_priorities.h"

#define TAG "MIDI_IN_UART"
#define MIDI_NUM       UART_NUM_1
#define RX_BUF_SIZE    256
#define UART_READ_TIMEOUT  20  // in milliseconds

static bool s_initialized = false;

static void midi_in_uart_task(void *pvParameters) {
  uint8_t rx_buf[RX_BUF_SIZE];
  bool was_connected = false;
  
  while (1) {
    // Check if MIDI IN cable is connected
    bool is_connected = gpio_get_level(PIN_MIDI_SW) == 1;
    
    // If cable detection is disabled, treat as always connected
    if (!input_get_cable_detection_enabled()) {
      is_connected = true;
    }
    
    // Log connection state changes
    if (is_connected != was_connected) {
      if (is_connected) {
        ESP_LOGI(TAG, "MIDI IN cable connected");
      } else {
        ESP_LOGI(TAG, "MIDI IN cable disconnected");
      }
      was_connected = is_connected;
    }
    
        // Only read UART when cable is connected
        if (is_connected) {
          int len = uart_read_bytes(MIDI_NUM, rx_buf, RX_BUF_SIZE,
                                    UART_READ_TIMEOUT / portTICK_PERIOD_MS);
          if (len > 0) {
            // ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);
            midi_in_process_stream(rx_buf, len, MIDI_SOURCE_UART);
          }
        }
    
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

esp_err_t midi_in_uart_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "UART MIDI IN already initialized");
    return ESP_OK;
  }

  // Configure MIDI IN cable detection pin
  gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << PIN_MIDI_SW),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_conf);
  
  // Create UART reading task
  xTaskCreate(midi_in_uart_task, "midi_in_uart", 4096, NULL, TASK_PRIORITY_MIDI_IN, NULL);
  
  // Check initial cable state
  bool cable_connected = gpio_get_level(PIN_MIDI_SW) == 1;
  ESP_LOGI(TAG, "UART MIDI IN initialized - Cable: %s (GPIO %d = %d)", 
    cable_connected ? "CONNECTED" : "DISCONNECTED", 
    PIN_MIDI_SW, gpio_get_level(PIN_MIDI_SW));

  s_initialized = true;
  return ESP_OK;
}

void midi_in_uart_deinit(void) {
  if (!s_initialized) return;
  
  s_initialized = false;
  ESP_LOGI(TAG, "UART MIDI IN deinitialized");
}

bool midi_in_uart_is_initialized(void) {
  return s_initialized;
}


