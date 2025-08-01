#ifndef TOUCH_SPI_MASTER_H
#define TOUCH_SPI_MASTER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_SPI_MASTER_MAX_EVENTS_PER_TRANSFER 6
#define TOUCH_SPI_MASTER_EMPTY_SLOT 0xFF
#define TOUCH_SPI_MASTER_OVERFLOW_EVENT 0xFE

#define ENCODE_TOUCH_EVENT(pad, pressed) (((pad) & 0x1F) | ((pressed) ? 0x20 : 0x00))
#define DECODE_PAD_NUM(event) ((event) & 0x1F)
#define IS_PRESSED(event) (((event) & 0x20) != 0)

typedef void (*touch_spi_master_event_callback_t)(uint8_t pad_num, bool is_pressed);

esp_err_t touch_spi_master_init(void);

esp_err_t touch_spi_master_deinit(void);

void touch_spi_master_register_event_callback(touch_spi_master_event_callback_t callback);

void touch_spi_master_get_stats(uint32_t *total_events, uint32_t *overflow_events);

size_t touch_spi_master_poll_once(void);

#endif // TOUCH_SPI_MASTER_H 