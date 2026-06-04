#ifndef _USB_CDC_UPDATE_H
#define _USB_CDC_UPDATE_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Initialize USB CDC update handler
 * Initializes CDC port and command protocol
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_update_init(void);

/**
 * CDC task - call periodically to handle incoming commands
 * Should be called from main loop or dedicated task
 */
void usb_cdc_task(void);

/**
 * Check if CDC update is currently in progress
 * @return true if update is ongoing
 */
bool usb_cdc_update_in_progress(void);

/**
 * Get current update progress (0-100)
 * @return Progress percentage
 */
uint8_t usb_cdc_update_get_progress(void);

/** Push EVT:programming:0|1 when app mode changes (web scene editor lock). */
void usb_cdc_notify_programming(bool active);

#endif /* _USB_CDC_UPDATE_H */

