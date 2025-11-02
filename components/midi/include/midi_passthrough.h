#ifndef _MIDI_PASSTHROUGH_H_
#define _MIDI_PASSTHROUGH_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MIDI passthrough and load settings from NVS
 * @return ESP_OK on success
 */
esp_err_t midi_passthrough_init(void);

/**
 * @brief Enable/disable USB → UART passthrough (saves to NVS)
 * @param enable true to enable, false to disable
 */
void midi_passthrough_usb_to_uart_enable(bool enable);

/**
 * @brief Enable/disable UART → USB passthrough (saves to NVS)
 * @param enable true to enable, false to disable
 */
void midi_passthrough_uart_to_usb_enable(bool enable);

/**
 * @brief Check if USB → UART passthrough is enabled
 * @return true if enabled, false otherwise
 */
bool midi_passthrough_usb_to_uart_is_enabled(void);

/**
 * @brief Check if UART → USB passthrough is enabled
 * @return true if enabled, false otherwise
 */
bool midi_passthrough_uart_to_usb_is_enabled(void);

/**
 * @brief Forward MIDI data from USB to UART (called by midi_in_usb)
 * @param data MIDI data bytes
 * @param len Length of data
 */
void midi_passthrough_forward_from_usb(const uint8_t *data, size_t len);

/**
 * @brief Forward MIDI data from UART to USB (called by midi_in_process_stream)
 * @param data MIDI data bytes
 * @param len Length of data
 */
void midi_passthrough_forward_from_uart(const uint8_t *data, size_t len);

/**
 * @brief Get passthrough statistics
 */
void midi_passthrough_get_stats(
  uint32_t *usb_to_uart_bytes,
  uint32_t *uart_to_usb_bytes,
  uint32_t *usb_to_uart_messages,
  uint32_t *uart_to_usb_messages
);

/**
 * @brief Reset statistics counters
 */
void midi_passthrough_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_PASSTHROUGH_H_ */

