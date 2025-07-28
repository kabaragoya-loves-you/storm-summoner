#ifndef TOUCH_SPI_MASTER_H
#define TOUCH_SPI_MASTER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Configuration options
#define USE_INTERRUPT_MODE 1  // Set to 0 for polling mode, 1 for interrupt mode

// Protocol constants (matching slave)
#define TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER 6
#define TOUCH_SPI_MASTER_EMPTY_SLOT 0xFF
#define TOUCH_SPI_MASTER_OVERFLOW_EVENT 0xFE

// Event encoding/decoding macros (matching slave protocol)
#define ENCODE_TOUCH_EVENT(pad, pressed) (((pad) & 0x1F) | ((pressed) ? 0x20 : 0x00))
#define DECODE_PAD_NUM(event) ((event) & 0x1F)
#define IS_PRESSED(event) (((event) & 0x20) != 0)

// Callback type for touch events
typedef void (*touch_spi_master_event_callback_t)(uint8_t pad_num, bool is_pressed);

/**
 * @brief Initialize the touch SPI master for touch event communication
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_spi_master_init(void);

/**
 * @brief Deinitialize the touch SPI master
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t touch_spi_master_deinit(void);

/**
 * @brief Register callback for touch events
 * 
 * @param callback Function to call when touch events are received
 */
void touch_spi_master_register_event_callback(touch_spi_master_event_callback_t callback);

/**
 * @brief Get current statistics
 * 
 * @param total_events Total events received
 * @param overflow_events Number of overflow events received
 */
void touch_spi_master_get_stats(uint32_t *total_events, uint32_t *overflow_events);

/**
 * @brief Force a manual poll (useful for debugging)
 * 
 * @return Number of events received
 */
size_t touch_spi_master_poll_once(void);

#endif // TOUCH_SPI_MASTER_H 