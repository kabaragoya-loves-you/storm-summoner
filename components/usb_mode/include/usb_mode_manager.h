#ifndef _USB_MODE_MANAGER_H
#define _USB_MODE_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
  USB_MODE_MIDI,
  USB_MODE_MSC
} usb_mode_t;

/**
 * Initialize USB mode manager
 * @return ESP_OK on success
 */
esp_err_t usb_mode_manager_init(void);

/**
 * Get current USB mode
 * @return Current USB mode
 */
usb_mode_t usb_mode_get_current(void);

/**
 * Switch USB to MIDI mode
 * @return ESP_OK on success
 */
esp_err_t usb_switch_to_midi(void);

/**
 * Switch USB to MSC (Mass Storage) mode for firmware updates
 * @return ESP_OK on success
 */
esp_err_t usb_switch_to_msc(void);

/**
 * Check if USB is ready in current mode
 * @return true if ready
 */
bool usb_mode_is_ready(void);

#endif /* _USB_MODE_MANAGER_H */

