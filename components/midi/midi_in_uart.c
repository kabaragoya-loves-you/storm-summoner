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
static bool s_cable_connected = false;

static void midi_in_uart_task(void *pvParameters) {
  uint8_t rx_buf[RX_BUF_SIZE];
  bool was_connected = s_cable_connected;
  uint8_t stable_count = 0;
  const uint8_t DEBOUNCE_COUNT = 3;  // Require 3 consecutive stable readings
  
  while (1) {
    // Check if MIDI IN cable is connected
    int gpio_level = gpio_get_level(PIN_MIDI_SW);
    bool current_reading = (gpio_level == 1);  // HIGH = connected
    
    // If cable detection is disabled, treat as always connected
    if (!input_get_cable_detection_enabled()) {
      current_reading = true;
    }
    
    // Simple debouncing - require N consecutive stable readings
    if (current_reading != was_connected) {
      stable_count++;
      if (stable_count >= DEBOUNCE_COUNT) {
        was_connected = current_reading;
        stable_count = 0;
        s_cable_connected = was_connected;

        // Log connection state changes
        if (was_connected) {
          ESP_LOGI(TAG, "MIDI IN cable connected");
        } else {
          ESP_LOGI(TAG, "MIDI IN cable disconnected");
        }
        event_bus_post_connections_changed();
      }
    } else {
      stable_count = 0;
    }
    
    // Only read UART when cable is connected
    if (was_connected) {
      int len = uart_read_bytes(MIDI_NUM, rx_buf, RX_BUF_SIZE,
                                UART_READ_TIMEOUT / portTICK_PERIOD_MS);
      if (len > 0) {
        // ESP_LOGI(TAG, "Received %d bytes", len);
        // ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, len, ESP_LOG_DEBUG);
        midi_in_process_stream(rx_buf, len, MIDI_SOURCE_UART);
      }
    }
    
    // Adaptive delay based on cable state:
    // - Cable connected: 10ms (handles full MIDI bandwidth with 6.5x buffer margin, low latency)
    // - Cable disconnected: 1000ms (minimal CPU usage)
    vTaskDelay(was_connected ? pdMS_TO_TICKS(10) : pdMS_TO_TICKS(1000));
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

  int gpio_level = gpio_get_level(PIN_MIDI_SW);
  bool cable_connected = (gpio_level == 1);
  if (!input_get_cable_detection_enabled()) cable_connected = true;
  s_cable_connected = cable_connected;

  xTaskCreate(midi_in_uart_task, "midi_in_uart", 4096, NULL, TASK_PRIORITY_MIDI_IN, NULL);

  ESP_LOGI(TAG, "UART MIDI IN initialized - Cable: %s (GPIO %d = %d)",
    cable_connected ? "CONNECTED" : "DISCONNECTED",
    PIN_MIDI_SW, gpio_level);

  s_initialized = true;
  return ESP_OK;
}

bool midi_in_uart_is_cable_connected(void) {
  if (!input_get_cable_detection_enabled()) return true;
  return s_cable_connected;
}

void midi_in_uart_deinit(void) {
  if (!s_initialized) return;
  
  s_initialized = false;
  ESP_LOGI(TAG, "UART MIDI IN deinitialized");
}

bool midi_in_uart_is_initialized(void) {
  return s_initialized;
}


