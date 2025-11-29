#include "usb_cdc_update.h"
#include "firmware_update.h"
#include "tusb.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_vfs.h"
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include "esp_system.h"

#define TAG "USB_CDC_UPDATE"

#define CDC_RX_BUF_SIZE 1024
#define CDC_CMD_BUF_SIZE 256
#define LOG_BUF_SIZE 512
#define CDC_VFS_PATH "/dev/cdccon"

// Update protocol states
typedef enum {
  CDC_STATE_IDLE,
  CDC_STATE_RECEIVING_FIRMWARE,
  CDC_STATE_RECEIVING_ASSETS,
  CDC_STATE_WAITING_COMMIT,
  CDC_STATE_COMMITTING,
  CDC_STATE_CONSOLE,  // Interactive console mode
  CDC_STATE_ERROR
} cdc_update_state_t;

static cdc_update_state_t s_state = CDC_STATE_IDLE;
static bool s_initialized = false;
static uint8_t *s_update_buffer = NULL;
static size_t s_update_size = 0;
static size_t s_received_bytes = 0;
static bool s_is_firmware = false;

static char s_cmd_buffer[CDC_CMD_BUF_SIZE];
static size_t s_cmd_pos = 0;

// Console mode state
static vprintf_like_t s_original_vprintf = NULL;
static bool s_log_redirect_active = false;
static FILE *s_saved_stdout = NULL;
static bool s_vfs_registered = false;

// ============================================================================
// VFS wrapper for CDC stdout redirect
// ============================================================================

static int cdc_vfs_open(const char *path, int flags, int mode) {
  (void)path; (void)flags; (void)mode;
  return 0;  // fd 0 for our single "file"
}

static int cdc_vfs_close(int fd) {
  (void)fd;
  return 0;
}

static ssize_t cdc_vfs_write(int fd, const void *data, size_t size) {
  (void)fd;
  if (!tud_cdc_n_connected(0)) {
    errno = EIO;
    return -1;
  }
  
  const char *ptr = (const char *)data;
  size_t written = 0;
  
  while (written < size) {
    // Handle newline conversion (LF -> CRLF for terminal)
    if (ptr[written] == '\n') {
      tud_cdc_n_write_char(0, '\r');
    }
    tud_cdc_n_write_char(0, ptr[written]);
    written++;
  }
  tud_cdc_n_write_flush(0);
  
  return written;
}

static int cdc_vfs_fstat(int fd, struct stat *st) {
  (void)fd;
  memset(st, 0, sizeof(*st));
  st->st_mode = S_IFCHR;
  return 0;
}

static int cdc_vfs_fcntl(int fd, int cmd, int arg) {
  (void)fd; (void)arg;
  if (cmd == F_GETFL) return O_WRONLY;
  if (cmd == F_SETFL) return 0;
  errno = ENOSYS;
  return -1;
}

static esp_err_t cdc_vfs_register(void) {
  if (s_vfs_registered) return ESP_OK;
  
  esp_vfs_t vfs = {
    .flags = ESP_VFS_FLAG_DEFAULT,
    .open = &cdc_vfs_open,
    .close = &cdc_vfs_close,
    .write = &cdc_vfs_write,
    .fstat = &cdc_vfs_fstat,
    .fcntl = &cdc_vfs_fcntl,
  };
  
  esp_err_t err = esp_vfs_register(CDC_VFS_PATH, &vfs, NULL);
  if (err == ESP_OK) {
    s_vfs_registered = true;
    ESP_LOGI(TAG, "CDC VFS registered at %s", CDC_VFS_PATH);
  }
  return err;
}

static void console_redirect_stdout(bool enable) {
  if (enable) {
    // Save current stdout and redirect to CDC
    fflush(stdout);
    s_saved_stdout = stdout;
    stdout = fopen(CDC_VFS_PATH, "w");
    if (!stdout) {
      ESP_LOGE(TAG, "Failed to redirect stdout to CDC");
      stdout = s_saved_stdout;
      s_saved_stdout = NULL;
    } else {
      // Disable buffering for immediate output
      setvbuf(stdout, NULL, _IONBF, 0);
    }
  } else {
    // Restore original stdout
    if (s_saved_stdout) {
      fflush(stdout);
      fclose(stdout);
      stdout = s_saved_stdout;
      s_saved_stdout = NULL;
    }
  }
}

// ============================================================================

// Forward declarations
static void process_command(const char *cmd);
static void process_console_command(const char *cmd);
static void send_response(const char *msg);
static void handle_binary_data(const uint8_t *data, size_t len);
static void console_send(const char *str);
static void console_send_prompt(void);

// Log redirect function - sends to CDC when in console mode
static int cdc_log_vprintf(const char *fmt, va_list args) {
  char buf[LOG_BUF_SIZE];
  int len = vsnprintf(buf, sizeof(buf), fmt, args);

  // Send to CDC if connected and in console mode
  if (s_log_redirect_active && tud_cdc_n_connected(0)) {
    size_t write_len = (len < LOG_BUF_SIZE) ? len : LOG_BUF_SIZE - 1;
    tud_cdc_n_write(0, buf, write_len);
    tud_cdc_n_write_flush(0);
  }

  // Write to original JTAG destination (use saved stdout if redirected)
  FILE *dest = s_saved_stdout ? s_saved_stdout : stdout;
  fputs(buf, dest);
  return len;
}

// CDC Update Task
static void cdc_update_task(void *arg) {
  ESP_LOGI(TAG, "CDC update task started");
  while (1) {
    usb_cdc_task();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t usb_cdc_update_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "CDC update already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing CDC update handler");

  // Register VFS for console stdout redirect
  cdc_vfs_register();

  // CDC is initialized by the composite descriptor
  // No additional initialization needed - TinyUSB handles it

  // Create task to poll CDC
  // Stack size increased for printf/VFS operations in console mode
  xTaskCreate(cdc_update_task, "cdc_update", 8192, NULL, 5, NULL);

  s_initialized = true;
  ESP_LOGI(TAG, "CDC update handler initialized");
  return ESP_OK;
}

void usb_cdc_task(void) {
  if (!s_initialized) return;

  // Check if CDC is connected
  if (!tud_cdc_n_connected(0)) {
    // If we were in console mode and disconnected, exit console mode
    if (s_state == CDC_STATE_CONSOLE) {
      console_redirect_stdout(false);  // Restore stdout first
      s_log_redirect_active = false;
      s_state = CDC_STATE_IDLE;
      ESP_LOGI(TAG, "CDC disconnected, exiting console mode");
    }
    return;
  }

  // Read available data
  if (tud_cdc_n_available(0)) {
    uint8_t buf[CDC_RX_BUF_SIZE];
    uint32_t count = tud_cdc_n_read(0, buf, sizeof(buf));
    
    if (count > 0) {
      if (s_state == CDC_STATE_IDLE || s_state == CDC_STATE_WAITING_COMMIT) {
        // In idle/waiting state, parse commands (text mode)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_CONSOLE) {
        // In console mode, handle interactive input with echo
        for (uint32_t i = 0; i < count; i++) {
          uint8_t ch = buf[i];
          
          // Echo the character
          tud_cdc_n_write_char(0, ch);
          tud_cdc_n_write_flush(0);
          
          if (ch == '\r' || ch == '\n') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              console_send("\r\n");
              process_console_command(s_cmd_buffer);
              s_cmd_pos = 0;
            } else {
              console_send_prompt();
            }
          } else if (ch == '\b' || ch == 0x7F) {
            // Backspace
            if (s_cmd_pos > 0) {
              s_cmd_pos--;
              console_send("\b \b");
            }
          } else if (ch == 0x03) {
            // Ctrl+C - cancel input
            s_cmd_pos = 0;
            console_send("^C\r\n");
            console_send_prompt();
          } else if (ch >= 0x20 && ch < 0x7F) {
            // Printable character
            if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
              s_cmd_buffer[s_cmd_pos++] = ch;
            }
          }
        }
      } else if (s_state == CDC_STATE_RECEIVING_FIRMWARE || s_state == CDC_STATE_RECEIVING_ASSETS) {
        // In receiving state, handle binary data
        handle_binary_data(buf, count);
      }
    }
  }
}

bool usb_cdc_update_in_progress(void) {
  return s_state != CDC_STATE_IDLE && s_state != CDC_STATE_ERROR;
}

uint8_t usb_cdc_update_get_progress(void) {
  if (s_update_size == 0) return 0;
  return (uint8_t)((s_received_bytes * 100) / s_update_size);
}

static void send_response(const char *msg) {
  if (!tud_cdc_n_connected(0)) return;
  
  tud_cdc_n_write(0, msg, strlen(msg));
  tud_cdc_n_write_char(0, '\n');
  tud_cdc_n_write_flush(0);
}

// Console mode helpers - use printf since stdout is redirected to CDC
static void console_send(const char *str) {
  // When in console mode, stdout is redirected to CDC
  // So we can just use fputs
  if (s_state == CDC_STATE_CONSOLE) {
    fputs(str, stdout);
    fflush(stdout);
  } else {
    // Fallback to direct CDC write if not in console mode
    if (!tud_cdc_n_connected(0)) return;
    tud_cdc_n_write(0, str, strlen(str));
    tud_cdc_n_write_flush(0);
  }
}

static void console_send_prompt(void) {
  printf("\n> ");
  fflush(stdout);
}

static void process_command(const char *cmd) {
  ESP_LOGI(TAG, "Received command: '%s' (len=%d)", cmd, strlen(cmd));
  
  // Debug hex dump of command
  char hex[128] = {0};
  for (int i = 0; i < strlen(cmd) && i < 16; i++) {
    snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02X ", (uint8_t)cmd[i]);
  }
  ESP_LOGI(TAG, "Hex: %s", hex);

  if (strncmp(cmd, "FIRMWARE ", 9) == 0) {
    // Parse size
    size_t size = atoi(cmd + 9);
    
    if (size == 0 || size > 8 * 1024 * 1024) {  // Max 8MB
      send_response("ERROR: Invalid firmware size");
      return;
    }

    ESP_LOGI(TAG, "Starting firmware update (%u bytes)", (unsigned)size);

    // Allocate buffer in PSRAM
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
    }
    
    s_update_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!s_update_buffer) {
      ESP_LOGE(TAG, "Failed to allocate %u bytes for firmware", (unsigned)size);
      send_response("ERROR: Memory allocation failed");
      s_state = CDC_STATE_ERROR;
      return;
    }

    s_update_size = size;
    s_received_bytes = 0;
    s_is_firmware = true;
    s_state = CDC_STATE_RECEIVING_FIRMWARE;
    send_response("READY");

  } else if (strncmp(cmd, "ASSETS ", 7) == 0) {
    // Parse size
    size_t size = atoi(cmd + 7);
    
    if (size == 0 || size > 16 * 1024 * 1024) {  // Max 16MB
      send_response("ERROR: Invalid assets size");
      return;
    }

    ESP_LOGI(TAG, "Starting assets update (%u bytes)", (unsigned)size);

    // Allocate buffer in PSRAM
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
    }
    
    s_update_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!s_update_buffer) {
      ESP_LOGE(TAG, "Failed to allocate %u bytes for assets", (unsigned)size);
      send_response("ERROR: Memory allocation failed");
      s_state = CDC_STATE_ERROR;
      return;
    }

    s_update_size = size;
    s_received_bytes = 0;
    s_is_firmware = false;
    s_state = CDC_STATE_RECEIVING_ASSETS;
    send_response("READY");

  } else if (strcmp(cmd, "COMMIT") == 0) {
    if (s_state != CDC_STATE_WAITING_COMMIT) {
      send_response("ERROR: No update pending commit");
      return;
    }

    if (s_received_bytes != s_update_size) {
      ESP_LOGW(TAG, "Incomplete transfer: received %u of %u bytes",
        (unsigned)s_received_bytes, (unsigned)s_update_size);
      send_response("ERROR: Incomplete transfer");
      s_state = CDC_STATE_ERROR;
      return;
    }

    s_state = CDC_STATE_COMMITTING;
    ESP_LOGI(TAG, "Committing %s update", s_is_firmware ? "firmware" : "assets");

    esp_err_t err;
    if (s_is_firmware) {
      err = firmware_update_start(s_update_buffer, s_update_size);
      if (err == ESP_OK) {
        err = firmware_update_write(s_update_buffer, s_update_size);
      }
      if (err == ESP_OK) {
        err = firmware_update_finalize();
      }
    } else {
      err = assets_update_start(s_update_buffer, s_update_size);
      if (err == ESP_OK) {
        err = assets_update_write(s_update_buffer, s_update_size);
      }
      if (err == ESP_OK) {
        err = assets_update_finalize();
      }
    }

    if (err == ESP_OK) {
      ESP_LOGI(TAG, "Update successful");
      send_response("SUCCESS");
      s_state = CDC_STATE_IDLE;
    } else {
      ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
      send_response("ERROR: Update failed");
      s_state = CDC_STATE_ERROR;
    }

    // Free buffer
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
      s_update_buffer = NULL;
    }
    s_update_size = 0;
    s_received_bytes = 0;

  } else if (strcmp(cmd, "RESET") == 0) {
    ESP_LOGI(TAG, "Reset command received. Rebooting...");
    send_response("RESETTING");
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time to send response
    esp_restart();

  } else if (strcmp(cmd, "STATUS") == 0) {
    uint8_t progress = usb_cdc_update_get_progress();
    char resp[32];
    snprintf(resp, sizeof(resp), "PROGRESS %u", progress);
    send_response(resp);

  } else if (strcmp(cmd, "CANCEL") == 0) {
    ESP_LOGI(TAG, "Update cancelled");
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
      s_update_buffer = NULL;
    }
    s_state = CDC_STATE_IDLE;
    s_update_size = 0;
    s_received_bytes = 0;
    send_response("CANCELLED");

  } else if (strcmp(cmd, "CONSOLE") == 0) {
    ESP_LOGI(TAG, "Entering console mode");
    s_state = CDC_STATE_CONSOLE;
    s_log_redirect_active = true;
    
    // Set up log redirect if not already done
    if (!s_original_vprintf) {
      s_original_vprintf = esp_log_set_vprintf(cdc_log_vprintf);
    }
    
    // Send protocol response before redirecting stdout
    send_response("CONSOLE_STARTED");
    
    // Redirect stdout to CDC for printf output from console commands
    console_redirect_stdout(true);
    
    printf("\r\n=== Storm Summoner Console ===\r\n");
    printf("Type 'help' for commands, 'contexts' for subsystems\r\n");
    printf("Type 'exit' to return to update mode\r\n");
    printf("\r\n> ");

  } else {
    ESP_LOGW(TAG, "Unknown command: %s", cmd);
    send_response("ERROR: Unknown command");
  }
}

static void handle_binary_data(const uint8_t *data, size_t len) {
  // Debug first chunk
  if (s_received_bytes == 0) {
    ESP_LOGI(TAG, "Received first chunk of data (%u bytes)", (unsigned)len);
  }

  if (!s_update_buffer) {
    ESP_LOGE(TAG, "No buffer allocated");
    s_state = CDC_STATE_ERROR;
    return;
  }

  size_t remaining = s_update_size - s_received_bytes;
  size_t to_copy = (len < remaining) ? len : remaining;

  memcpy(s_update_buffer + s_received_bytes, data, to_copy);
  s_received_bytes += to_copy;

  // Send progress updates every 10%
  static uint8_t last_progress = 0;
  uint8_t progress = usb_cdc_update_get_progress();
  if (progress >= last_progress + 10) {
    char resp[32];
    snprintf(resp, sizeof(resp), "PROGRESS %u", progress);
    send_response(resp);
    last_progress = progress;
  }

  if (s_received_bytes >= s_update_size) {
    ESP_LOGI(TAG, "Transfer complete (%u bytes)", (unsigned)s_received_bytes);
    send_response("TRANSFER_COMPLETE");
    s_state = CDC_STATE_WAITING_COMMIT;
  }
}

// Process console commands via esp_console
static void process_console_command(const char *cmd) {
  if (strlen(cmd) == 0) {
    console_send_prompt();
    return;
  }

  // Check for exit command
  if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "EXIT") == 0) {
    printf("Exiting console mode...\r\n");
    
    // Restore stdout before changing state
    console_redirect_stdout(false);
    
    ESP_LOGI(TAG, "Exiting console mode");
    s_log_redirect_active = false;
    s_state = CDC_STATE_IDLE;
    send_response("CONSOLE_STOPPED");
    return;
  }

  // Run through esp_console
  int ret;
  esp_err_t err = esp_console_run(cmd, &ret);

  if (err == ESP_ERR_NOT_FOUND) {
    printf("Unknown command: %s\n", cmd);
  } else if (err == ESP_ERR_INVALID_ARG) {
    printf("Invalid arguments\n");
  } else if (err != ESP_OK) {
    printf("Error: %s\n", esp_err_to_name(err));
  }

  console_send_prompt();
}

