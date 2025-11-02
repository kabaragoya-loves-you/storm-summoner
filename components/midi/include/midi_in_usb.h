#ifndef _MIDI_IN_USB_H
#define _MIDI_IN_USB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialize USB MIDI input
 * @return ESP_OK on success
 */
esp_err_t midi_in_usb_init(void);

/**
 * Deinitialize USB MIDI input
 */
void midi_in_usb_deinit(void);

/**
 * Check if USB MIDI IN is initialized
 * @return true if initialized
 */
bool midi_in_usb_is_initialized(void);

/**
 * Process incoming USB MIDI packet
 * Called from TinyUSB RX callback
 * @param packet USB MIDI packet data
 * @param len Length of packet
 */
void midi_in_usb_process_packet(const uint8_t *packet, size_t len);

#endif /* _MIDI_IN_USB_H */

