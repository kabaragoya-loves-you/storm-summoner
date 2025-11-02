#ifndef _MIDI_SYSEX_UPDATE_H
#define _MIDI_SYSEX_UPDATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Custom SysEx manufacturer ID (0x7D = educational/development use)
#define SYSEX_MANUFACTURER_ID 0x7D

// SysEx update commands
#define SYSEX_CMD_FIRMWARE_CHUNK 0x01
#define SYSEX_CMD_ASSETS_CHUNK   0x02
#define SYSEX_CMD_FIRMWARE_COMMIT 0x03
#define SYSEX_CMD_ASSETS_COMMIT   0x04
#define SYSEX_CMD_STATUS_REQUEST  0x05
#define SYSEX_CMD_STATUS_RESPONSE 0x06

/**
 * Initialize SysEx update handler
 * Registers callback with MIDI IN
 * @return ESP_OK on success
 */
esp_err_t midi_sysex_update_init(void);

/**
 * Process incoming SysEx message
 * Called by MIDI IN SysEx callback
 * @param data SysEx message data (including F0 and F7)
 * @param len Length of message
 * @return ESP_OK on success
 */
esp_err_t midi_sysex_update_process(const uint8_t *data, size_t len);

/**
 * Get current update progress (0-100)
 * @return Progress percentage
 */
uint8_t midi_sysex_update_get_progress(void);

/**
 * Send status response via SysEx
 * @param status_code Status code to send
 * @return ESP_OK on success
 */
esp_err_t midi_sysex_send_status(uint8_t status_code);

#endif /* _MIDI_SYSEX_UPDATE_H */

