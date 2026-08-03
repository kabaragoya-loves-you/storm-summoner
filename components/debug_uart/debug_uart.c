#include "debug_uart.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "app_settings.h"

#define TAG "DEBUG_UART"
#define LOG_BUF_SIZE 512
#define REPL_LINE_MAX 256
#define REPL_TASK_STACK 4096
#define REPL_TASK_PRIO 5

static vprintf_like_t s_prev_vprintf;
static bool s_enabled;
static bool s_driver_installed;
static bool s_repl_started;
static volatile bool s_in_vprintf;

static int uart_mirror_vprintf(const char *fmt, va_list args) {
  if (!fmt) return 0;

  // Re-entrancy guard: never log from inside the mirror path.
  if (s_in_vprintf)
    return s_prev_vprintf ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);

  s_in_vprintf = true;

  va_list args_copy;
  va_copy(args_copy, args);
  char buf[LOG_BUF_SIZE];
  int n = vsnprintf(buf, sizeof(buf), fmt, args_copy);
  va_end(args_copy);

  if (n > 0 && s_driver_installed) {
    size_t len = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
    uart_write_bytes(DEBUG_UART_NUM, buf, len);
  }

  int ret = s_prev_vprintf ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);
  s_in_vprintf = false;
  return ret;
}

bool debug_uart_nvs_enabled(void) {
  bool enabled = false;
  if (app_settings_load_bool(NVS_KEY_DEBUG_UART, &enabled) != APP_SETTINGS_OK)
    return false;
  return enabled;
}

esp_err_t debug_uart_set_nvs_enabled(bool enabled) {
  return app_settings_save_bool(NVS_KEY_DEBUG_UART, enabled);
}

bool debug_uart_is_enabled(void) {
  return s_enabled;
}

esp_err_t debug_uart_enable(void) {
  if (s_enabled)
    return ESP_OK;

#if CONFIG_ESP_CONSOLE_UART
  // State 3: IDF already owns UART0 as the console (logs + panic on GPIO37).
  ESP_LOGI(TAG, "UART console already active via Kconfig; runtime mirror skipped");
  s_enabled = true;
  return ESP_OK;
#else
  uart_config_t cfg = {
    .baud_rate = DEBUG_UART_BAUD,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_driver_install(DEBUG_UART_NUM, 1024, 1024, 0, NULL, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_ERROR_CHECK(uart_param_config(DEBUG_UART_NUM, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(DEBUG_UART_NUM, DEBUG_UART_TX_GPIO, DEBUG_UART_RX_GPIO,
    UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  s_driver_installed = true;
  s_prev_vprintf = esp_log_set_vprintf(uart_mirror_vprintf);
  s_enabled = true;

  ESP_LOGI(TAG, "UART0 log mirror on GPIO%u TX / GPIO%u RX @ %d baud",
    (unsigned)DEBUG_UART_TX_GPIO, (unsigned)DEBUG_UART_RX_GPIO, DEBUG_UART_BAUD);
  return ESP_OK;
#endif
}

esp_err_t debug_uart_disable(void) {
  if (!s_enabled)
    return ESP_OK;

#if CONFIG_ESP_CONSOLE_UART
  s_enabled = false;
  return ESP_OK;
#else
  if (s_prev_vprintf) {
    esp_log_set_vprintf(s_prev_vprintf);
    s_prev_vprintf = NULL;
  }

  // Tell REPL task to exit, then wait before tearing down the driver.
  s_enabled = false;
  vTaskDelay(pdMS_TO_TICKS(200));

  if (s_driver_installed) {
    uart_driver_delete(DEBUG_UART_NUM);
    s_driver_installed = false;
  }

  s_repl_started = false;
  ESP_LOGI(TAG, "UART0 log mirror disabled");
  return ESP_OK;
#endif
}

#if !CONFIG_ESP_CONSOLE_UART
static void debug_uart_repl_task(void *arg) {
  (void)arg;
  char line[REPL_LINE_MAX];
  size_t pos = 0;

  const char *banner = "\r\n[debug_uart] console ready\r\n> ";
  uart_write_bytes(DEBUG_UART_NUM, banner, strlen(banner));

  while (s_enabled && s_driver_installed) {
    uint8_t ch;
    int n = uart_read_bytes(DEBUG_UART_NUM, &ch, 1, pdMS_TO_TICKS(100));
    if (n <= 0)
      continue;

    if (ch == '\r' || ch == '\n') {
      if (pos == 0) {
        uart_write_bytes(DEBUG_UART_NUM, "\r\n> ", 4);
        continue;
      }
      line[pos] = '\0';
      pos = 0;
      uart_write_bytes(DEBUG_UART_NUM, "\r\n", 2);

      int ret;
      esp_err_t err = esp_console_run(line, &ret);
      if (err == ESP_ERR_NOT_FOUND)
        uart_write_bytes(DEBUG_UART_NUM, "Unrecognized command\r\n", 22);
      else if (err == ESP_ERR_INVALID_ARG)
        uart_write_bytes(DEBUG_UART_NUM, "Invalid command\r\n", 17);
      else if (err == ESP_OK && ret != ESP_OK) {
        char msg[48];
        int m = snprintf(msg, sizeof(msg), "Command returned 0x%x\r\n", (unsigned)ret);
        if (m > 0)
          uart_write_bytes(DEBUG_UART_NUM, msg, (size_t)m);
      }

      uart_write_bytes(DEBUG_UART_NUM, "> ", 2);
      continue;
    }

    if (ch == 0x7f || ch == '\b') {
      if (pos > 0) {
        pos--;
        uart_write_bytes(DEBUG_UART_NUM, "\b \b", 3);
      }
      continue;
    }

    if (ch < 0x20 || pos + 1 >= sizeof(line))
      continue;

    line[pos++] = (char)ch;
    uart_write_bytes(DEBUG_UART_NUM, (const char *)&ch, 1);
  }

  s_repl_started = false;
  vTaskDelete(NULL);
}
#endif

esp_err_t debug_uart_start_console(void) {
  if (!s_enabled)
    return ESP_ERR_INVALID_STATE;

#if CONFIG_ESP_CONSOLE_UART
  // Primary console already provides UART stdin when wired.
  return ESP_OK;
#else
  if (!s_driver_installed || s_repl_started)
    return ESP_OK;

  BaseType_t ok = xTaskCreate(debug_uart_repl_task, "dbg_uart_repl",
    REPL_TASK_STACK, NULL, REPL_TASK_PRIO, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to start UART REPL task");
    return ESP_ERR_NO_MEM;
  }
  s_repl_started = true;
  return ESP_OK;
#endif
}
