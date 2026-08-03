#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include <stdbool.h>
#include "driver/uart.h"
#include "esp_err.h"

#define DEBUG_UART_TX_GPIO 37
#define DEBUG_UART_RX_GPIO 38
#define DEBUG_UART_BAUD 115200
#define DEBUG_UART_NUM UART_NUM_0

#define NVS_KEY_DEBUG_UART "dbg_uart"

/**
 * True if NVS has UART log mirror enabled for every boot.
 */
bool debug_uart_nvs_enabled(void);

/**
 * Persist the Settings toggle. Does not by itself start/stop the mirror;
 * call debug_uart_enable() / debug_uart_disable() for immediate effect.
 */
esp_err_t debug_uart_set_nvs_enabled(bool enabled);

/**
 * Install UART0 on GPIO37/38 and tee ESP_LOG* there while keeping the
 * primary console (USB Serial/JTAG). No-op if already enabled, or if
 * Kconfig already uses UART as the primary console (state 3).
 */
esp_err_t debug_uart_enable(void);

/**
 * Stop the log mirror and release UART0 if we installed it.
 */
esp_err_t debug_uart_disable(void);

bool debug_uart_is_enabled(void);

/**
 * Start a UART-side console task that runs the same esp_console command
 * table. Call after console_repl_init(). No-op if mirror is not enabled.
 */
esp_err_t debug_uart_start_console(void);

#endif // DEBUG_UART_H
