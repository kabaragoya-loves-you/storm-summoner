#include "midi_out_uart.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MIDI_OUT_UART"
#define MIDI_NUM UART_NUM_1

// MIDI timing: 31250 baud, 10 bits/byte = 0.32ms per byte
// Calculate minimum ticks needed for UART TX to complete + 1ms margin for GPIO settling
#define MIDI_TX_DELAY_TICKS(len) pdMS_TO_TICKS(((len) + 3))

static bool s_initialized = false;
static midi_transmit_mode_t s_current_mode = MIDI_TRANSMIT_BOTH;

esp_err_t midi_out_uart_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "UART MIDI already initialized");
    return ESP_OK;
  }

  uart_config_t uart_config = {
    .baud_rate = 31250,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  };
  
  ESP_ERROR_CHECK(uart_param_config(MIDI_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(MIDI_NUM, PIN_MIDI_TXD, PIN_MIDI_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(uart_driver_install(MIDI_NUM, 256, 0, 0, NULL, 0));

  uart_set_line_inverse(MIDI_NUM, UART_SIGNAL_TXD_INV);

  gpio_config_t io_polarity = {
    .pin_bit_mask = (1ULL << PIN_POLARITY),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_polarity);

  gpio_config_t io_ground = {
    .pin_bit_mask = (1ULL << PIN_MIDI_TS),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
  };
  gpio_config(&io_ground);

  // Initialize to TYPE_A state (both LOW)
  gpio_set_level(PIN_POLARITY, 0);
  gpio_set_level(PIN_MIDI_TS, 0);

  s_initialized = true;
  ESP_LOGI(TAG, "UART MIDI initialized successfully");
  return ESP_OK;
}

void midi_out_uart_deinit(void) {
  if (!s_initialized) return;
  
  uart_driver_delete(MIDI_NUM);
  s_initialized = false;
  ESP_LOGI(TAG, "UART MIDI deinitialized");
}

bool midi_out_uart_is_initialized(void) {
  return s_initialized;
}

esp_err_t midi_out_uart_send(const uint8_t *data, size_t len) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "UART MIDI not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!data || len == 0) return ESP_ERR_INVALID_ARG;

  // GPIO → optocoupler (inverts) → TMUX1113 SEL
  // GPIO=0 → SEL=1, GPIO=1 → SEL=0
  // TMUX1113: Ch1 ON@SEL1=1, Ch2 ON@SEL2=0, Ch3 ON@SEL3=0, Ch4 ON@SEL4=1
  // TYPE_A (data on tip):  GPIO=0 → SEL=1 → Ch1+Ch4 on (SNK→TIP, SRC→RING)
  // TYPE_B (data on ring): GPIO=1 → SEL=0 → Ch2+Ch3 on (SNK→RING, SRC→TIP)
  // TS (tip only):         POL=0, TS=1 → Ch1 on, Ch4 off (SNK→TIP, RING float)
  switch (s_current_mode) {
    case MIDI_TRANSMIT_BOTH:
      gpio_set_level(PIN_POLARITY, TRS_TYPE_A);
      gpio_set_level(PIN_MIDI_TS, TRS_TYPE_A);
      uart_write_bytes(MIDI_NUM, data, len);
      uart_wait_tx_done(MIDI_NUM, pdMS_TO_TICKS(100));
      gpio_set_level(PIN_POLARITY, TRS_TYPE_B);
      gpio_set_level(PIN_MIDI_TS, TRS_TYPE_B);
      uart_write_bytes(MIDI_NUM, data, len);
      break;

    case MIDI_TRANSMIT_TYPE_A:
      gpio_set_level(PIN_POLARITY, TRS_TYPE_A);
      gpio_set_level(PIN_MIDI_TS, TRS_TYPE_A);
      uart_write_bytes(MIDI_NUM, data, len);
      break;

    case MIDI_TRANSMIT_TYPE_B:
      gpio_set_level(PIN_POLARITY, TRS_TYPE_B);
      gpio_set_level(PIN_MIDI_TS, TRS_TYPE_B);
      uart_write_bytes(MIDI_NUM, data, len);
      break;

    case MIDI_TRANSMIT_TS:
      // True TS: data on TIP, RING floating (Ch4 OFF)
      // Note: High-Z RING shows capacitive ghost of TIP on analyzers,
      // but real TS pedals either ignore RING or ground it internally.
      gpio_set_level(PIN_POLARITY, TRS_TYPE_A);
      gpio_set_level(PIN_MIDI_TS, 1);
      uart_write_bytes(MIDI_NUM, data, len);
      break;
  }

  return ESP_OK;
}

void midi_out_uart_set_mode(midi_transmit_mode_t mode) {
  s_current_mode = mode;
  ESP_LOGD(TAG, "UART transmit mode set to %d", mode);
}

midi_transmit_mode_t midi_out_uart_get_mode(void) {
  return s_current_mode;
}
