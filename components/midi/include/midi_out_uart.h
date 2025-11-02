#ifndef _MIDI_OUT_UART_H
#define _MIDI_OUT_UART_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
  TYPE_A,
  TYPE_B
} polarity_t;

typedef enum {
  MIDI_TRANSMIT_BOTH,
  MIDI_TRANSMIT_TYPE_A,
  MIDI_TRANSMIT_TYPE_B,
  MIDI_TRANSMIT_TS
} midi_transmit_mode_t;

/**
 * Initialize UART MIDI output
 * @return ESP_OK on success
 */
esp_err_t midi_out_uart_init(void);

/**
 * Deinitialize UART MIDI output
 */
void midi_out_uart_deinit(void);

/**
 * Check if UART MIDI is initialized
 * @return true if initialized
 */
bool midi_out_uart_is_initialized(void);

/**
 * Send MIDI message via UART
 * @param data MIDI message data
 * @param len Length of message
 * @return ESP_OK on success
 */
esp_err_t midi_out_uart_send(const uint8_t *data, size_t len);

/**
 * Set UART transmit mode (TYPE_A, TYPE_B, BOTH, TS)
 * @param mode Transmit mode
 */
void midi_out_uart_set_mode(midi_transmit_mode_t mode);

/**
 * Get current UART transmit mode
 * @return Current transmit mode
 */
midi_transmit_mode_t midi_out_uart_get_mode(void);

#endif /* _MIDI_OUT_UART_H */

