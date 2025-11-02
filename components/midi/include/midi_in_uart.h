#ifndef _MIDI_IN_UART_H_
#define _MIDI_IN_UART_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UART MIDI IN transport
 * Sets up UART, GPIO for cable detection, and creates reading task
 * @return ESP_OK on success
 */
esp_err_t midi_in_uart_init(void);

/**
 * @brief Deinitialize UART MIDI IN transport
 */
void midi_in_uart_deinit(void);

/**
 * @brief Check if UART MIDI IN is initialized
 * @return true if initialized, false otherwise
 */
bool midi_in_uart_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* _MIDI_IN_UART_H_ */


