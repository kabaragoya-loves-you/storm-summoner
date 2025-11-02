#ifndef _MIDI_OUT_USB_H
#define _MIDI_OUT_USB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize USB MIDI output
 * @return ESP_OK on success
 */
esp_err_t midi_out_usb_init(void);

/**
 * Deinitialize USB MIDI output
 */
void midi_out_usb_deinit(void);

/**
 * Check if USB MIDI is initialized
 * @return true if initialized
 */
bool midi_out_usb_is_initialized(void);

/**
 * Check if USB MIDI is connected/enumerated
 * @return true if connected
 */
bool midi_out_usb_is_connected(void);

/**
 * Send MIDI message via USB
 * @param data MIDI message data
 * @param len Length of message
 * @return ESP_OK on success
 */
esp_err_t midi_out_usb_send(const uint8_t *data, size_t len);

#endif /* _MIDI_OUT_USB_H */

