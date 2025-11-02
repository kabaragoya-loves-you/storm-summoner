#ifndef _TINYUSB_INIT_H
#define _TINYUSB_INIT_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize TinyUSB and start the device task
 * @return ESP_OK on success
 */
esp_err_t tinyusb_init_and_start(void);

/**
 * Check if TinyUSB is initialized
 * @return true if initialized
 */
bool tinyusb_is_initialized(void);

/**
 * Check if USB device is mounted/enumerated
 * @return true if mounted
 */
bool tinyusb_is_mounted(void);

#endif /* _TINYUSB_INIT_H */

