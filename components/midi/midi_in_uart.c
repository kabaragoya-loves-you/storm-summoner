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
  
  // Majority voting debounce - collect samples over 50ms window
  #define VOTE_WINDOW 50  // 50 samples = 50ms (short window for clean signal)
  uint8_t vote_buffer[VOTE_WINDOW] = {0};
  uint8_t vote_index = 0;
  bool vote_buffer_filled = false;
  
  while (1) {
    // Check if MIDI IN cable is connected
    int gpio_level = gpio_get_level(PIN_MIDI_SW);
    bool current_reading = (gpio_level == 1);  // HIGH = connected
    
    // If cable detection is disabled, treat as always connected
    if (!input_get_cable_detection_enabled()) {
      current_reading = true;
    }
    
    // Add to vote buffer (circular)
    vote_buffer[vote_index] = current_reading ? 1 : 0;
    vote_index = (vote_index + 1) % VOTE_WINDOW;
    if (vote_index == 0) vote_buffer_filled = true;
    
    // Only process after buffer is filled
    if (vote_buffer_filled) {
      // Count votes
      uint8_t high_count = 0;
      for (int i = 0; i < VOTE_WINDOW; i++) {
        high_count += vote_buffer[i];
      }
      
      // Hysteresis thresholds to prevent oscillation
      // Need >80% to transition to CONNECTED
      // Need <20% to transition to DISCONNECTED
      // In between: maintain current state (allows for occasional noise spikes)
      bool voted_connected = was_connected;  // Default: keep current state
      if (high_count > (VOTE_WINDOW * 4 / 5)) {
        voted_connected = true;   // Strong HIGH signal (>80%)
      } else if (high_count < (VOTE_WINDOW / 5)) {
        voted_connected = false;  // Strong LOW signal (<20%)
      }
      // else: in hysteresis zone (20-80%), keep previous state
      
      // Debug: log every second
      // static uint32_t last_log = 0;
      // uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
      // if (now - last_log > 1000) {
      //   ESP_LOGI(TAG, "GPIO=%d, high_votes=%d/%d (%.0f%%), state=%d", 
      //     gpio_level, high_count, VOTE_WINDOW, (high_count * 100.0f / VOTE_WINDOW), voted_connected);
      //   last_log = now;
      // }
      
      // Check for state change
      if (voted_connected != was_connected) {
        was_connected = voted_connected;
        
        // Log connection state changes with vote percentage
        float percentage = (high_count * 100.0f / VOTE_WINDOW);
        if (voted_connected) {
          ESP_LOGI(TAG, "*** MIDI IN cable CONNECTED (%.0f%% high votes) ***", percentage);
        } else {
          ESP_LOGI(TAG, "*** MIDI IN cable DISCONNECTED (%.0f%% high votes) ***", percentage);
        }
      }
    }
    
    // Only read UART when cable is connected
    if (was_connected) {
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
  int gpio_level = gpio_get_level(PIN_MIDI_SW);
  bool cable_connected = (gpio_level == 1);  // HIGH = connected
  ESP_LOGI(TAG, "UART MIDI IN initialized - Cable: %s (GPIO %d = %d)", 
    cable_connected ? "CONNECTED" : "DISCONNECTED", 
    PIN_MIDI_SW, gpio_level);

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


