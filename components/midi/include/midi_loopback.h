#ifndef _MIDI_LOOPBACK_H_
#define _MIDI_LOOPBACK_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI loopback and load settings from NVS
 * @return ESP_OK on success
 */
esp_err_t midi_loopback_init(void);

/**
 * @brief Enable/disable UART loopback (UART IN → UART OUT, saves to NVS)
 * @param enable true to enable, false to disable
 */
void midi_loopback_uart_enable(bool enable);

/**
 * @brief Enable/disable USB loopback (USB IN → USB OUT, saves to NVS)
 * @param enable true to enable, false to disable
 */
void midi_loopback_usb_enable(bool enable);

/**
 * @brief Check if UART loopback is enabled
 * @return true if enabled, false otherwise
 */
bool midi_loopback_uart_is_enabled(void);

/**
 * @brief Check if USB loopback is enabled
 * @return true if enabled, false otherwise
 */
bool midi_loopback_usb_is_enabled(void);

/**
 * @brief Get loopback statistics
 */
void midi_loopback_get_stats(
  uint32_t *uart_bytes,
  uint32_t *usb_bytes,
  uint32_t *uart_messages,
  uint32_t *usb_messages
);

/**
 * @brief Reset statistics counters
 */
void midi_loopback_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_LOOPBACK_H_ */


