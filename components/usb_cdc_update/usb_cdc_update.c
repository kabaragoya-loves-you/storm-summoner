#include "usb_cdc_update.h"
#include "firmware_update.h"
#include "assets_file_ops.h"
#include "tusb.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_vfs.h"
#include "esp_littlefs.h"
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_system.h"

// Suppress warnings from miniz header (unused static functions)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "miniz/miniz.h"
#pragma GCC diagnostic pop

#define TAG "USB_CDC_UPDATE"

#define CDC_RX_BUF_SIZE 1024
#define CDC_CMD_BUF_SIZE 512
#define LOG_BUF_SIZE 512
#define CDC_VFS_PATH "/dev/cdccon"
#define ASSETS_BASE_PATH "/assets"
#define MAX_PATH_LEN 320  // Must accommodate paths + d_name (255)
#define CAT_MAX_SIZE 4096

// Update protocol states
typedef enum {
  CDC_STATE_IDLE,
  CDC_STATE_RECEIVING_FIRMWARE,
  CDC_STATE_RECEIVING_ASSETS,
  CDC_STATE_WAITING_COMMIT,
  CDC_STATE_COMMITTING,
  CDC_STATE_CONSOLE,  // Interactive console mode
  CDC_STATE_ASSETS,   // Assets management mode
  CDC_STATE_ASSETS_RECEIVING,  // Receiving file data in assets mode
  CDC_STATE_ASSETS_SENDING,    // Sending file data in assets mode
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

// Assets mode state
static char s_assets_path[MAX_PATH_LEN];  // Current file path for PUT/GET
static FILE *s_assets_file = NULL;        // Current file handle
static size_t s_assets_file_size = 0;     // Expected file size for PUT
static size_t s_assets_bytes_transferred = 0;  // Bytes transferred so far

// ZIP task state
static TaskHandle_t s_zip_task_handle = NULL;
static volatile bool s_zip_in_progress = false;
static char s_zip_path[MAX_PATH_LEN];     // Path for ZIP operation

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
static void process_assets_command(const char *cmd);
static void send_response(const char *msg);
static void send_binary(const uint8_t *data, size_t len);
static void handle_binary_data(const uint8_t *data, size_t len);
static void handle_assets_binary_data(const uint8_t *data, size_t len);
static void handle_assets_send(void);
static void console_send(const char *str);
static void console_send_prompt(void);

// Assets mode command handlers
static void assets_cmd_ls(const char *path);
static void assets_cmd_stat(const char *path);
static void assets_cmd_df(void);
static void assets_cmd_cat(const char *path);
static void assets_cmd_mkdir(const char *path);
static void assets_cmd_rm(const char *path);
static void assets_cmd_mv(const char *src, const char *dst);
static void assets_cmd_cp(const char *src, const char *dst);
static void assets_cmd_put(const char *path, size_t size);
static void assets_cmd_get(const char *path);
static void assets_cmd_manifest(const char *type);
static void assets_cmd_zip(const char *path);

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
  // Stack size for printf/VFS operations in console mode
  // ZIP operations spawn their own task with larger stack
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
    // If we were in assets mode and disconnected, cleanup
    if (s_state == CDC_STATE_ASSETS || s_state == CDC_STATE_ASSETS_RECEIVING || 
        s_state == CDC_STATE_ASSETS_SENDING) {
      if (s_assets_file) {
        fclose(s_assets_file);
        s_assets_file = NULL;
      }
      s_state = CDC_STATE_IDLE;
      ESP_LOGI(TAG, "CDC disconnected, exiting assets mode");
    }
    return;
  }
  
  // Handle sending file data in assets mode
  if (s_state == CDC_STATE_ASSETS_SENDING) {
    handle_assets_send();
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
      } else if (s_state == CDC_STATE_ASSETS) {
        // In assets mode, parse commands (text mode, no echo)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_assets_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_ASSETS_RECEIVING) {
        // Receiving file data in assets mode
        handle_assets_binary_data(buf, count);
      }
    }
  }
}

bool usb_cdc_update_in_progress(void) {
  return s_state != CDC_STATE_IDLE && s_state != CDC_STATE_ERROR && 
         s_state != CDC_STATE_ASSETS;  // Assets mode is not an "update"
}

uint8_t usb_cdc_update_get_progress(void) {
  if (s_update_size == 0) return 0;
  return (uint8_t)((s_received_bytes * 100) / s_update_size);
}

// Debug: log response to file for troubleshooting (uncomment when needed)
// static void log_response_to_file(const char *msg) {
//   FILE *f = fopen("/assets/debug_response.txt", "w");
//   if (f) {
//     fprintf(f, "len=%d\n%s\n", (int)strlen(msg), msg);
//     fclose(f);
//   }
// }

static void send_response(const char *msg) {
  if (!tud_cdc_n_connected(0)) return;
  
  // Debug: uncomment to log what we're about to send
  // log_response_to_file(msg);
  
  // Send message in chunks to handle long responses
  // Use smaller chunks with delays to ensure host can keep up
  const size_t chunk_size = 64;
  const int max_retries = 100;  // Max retries when buffer full
  size_t len = strlen(msg);
  size_t sent = 0;
  int retry_count = 0;
  
  while (sent < len) {
    // Check connection before each write attempt
    if (!tud_cdc_n_connected(0)) {
      ESP_LOGW(TAG, "CDC disconnected during send_response");
      return;
    }
    
    size_t to_send = (len - sent > chunk_size) ? chunk_size : (len - sent);
    uint32_t written = tud_cdc_n_write(0, msg + sent, to_send);
    tud_cdc_n_write_flush(0);
    sent += written;
    
    if (written == 0) {
      retry_count++;
      if (retry_count >= max_retries) {
        ESP_LOGW(TAG, "send_response: max retries reached, sent %u/%u bytes", (unsigned)sent, (unsigned)len);
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(5));  // Wait for buffer space
    } else {
      retry_count = 0;  // Reset retry counter on successful write
      vTaskDelay(pdMS_TO_TICKS(1));  // Small delay between chunks
    }
  }
  tud_cdc_n_write_char(0, '\n');
  tud_cdc_n_write_flush(0);
}

static void send_binary(const uint8_t *data, size_t len) {
  if (!tud_cdc_n_connected(0)) return;
  
  // Send in chunks to avoid overflow
  const size_t chunk_size = 512;
  const int max_retries = 100;  // Max retries when buffer full
  size_t sent = 0;
  int retry_count = 0;
  
  while (sent < len) {
    // Check connection before each write attempt
    if (!tud_cdc_n_connected(0)) {
      ESP_LOGW(TAG, "CDC disconnected during send_binary");
      return;
    }
    
    size_t to_send = (len - sent > chunk_size) ? chunk_size : len - sent;
    uint32_t written = tud_cdc_n_write(0, data + sent, to_send);
    tud_cdc_n_write_flush(0);
    sent += written;
    
    if (written == 0) {
      retry_count++;
      if (retry_count >= max_retries) {
        ESP_LOGW(TAG, "send_binary: max retries reached, sent %u/%u bytes", (unsigned)sent, (unsigned)len);
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(5));  // Wait for buffer space
    } else {
      retry_count = 0;  // Reset retry counter on successful write
    }
  }
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

  } else if (strcmp(cmd, "ASSETS") == 0) {
    ESP_LOGI(TAG, "Entering assets management mode");
    s_state = CDC_STATE_ASSETS;
    s_assets_path[0] = '\0';
    s_assets_file = NULL;
    s_assets_file_size = 0;
    s_assets_bytes_transferred = 0;
    send_response("ASSETS_STARTED");

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

// ============================================================================
// Assets Management Mode
// ============================================================================

// Build full path from relative path
static void build_full_path(char *dest, size_t dest_size, const char *path) {
  if (path[0] == '/') {
    // Absolute path - ensure it starts with /assets
    if (strncmp(path, ASSETS_BASE_PATH, strlen(ASSETS_BASE_PATH)) == 0) {
      strncpy(dest, path, dest_size - 1);
    } else {
      snprintf(dest, dest_size, "%s%s", ASSETS_BASE_PATH, path);
    }
  } else {
    // Relative path
    snprintf(dest, dest_size, "%s/%s", ASSETS_BASE_PATH, path);
  }
  dest[dest_size - 1] = '\0';
}

// Process assets mode commands
static void process_assets_command(const char *cmd) {
  ESP_LOGD(TAG, "Assets cmd: %s", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave assets mode
  if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "exit") == 0) {
    ESP_LOGI(TAG, "Exiting assets mode");
    s_state = CDC_STATE_IDLE;
    send_response("ASSETS_STOPPED");
    return;
  }
  
  // DF - filesystem stats
  if (strcmp(cmd, "DF") == 0) {
    assets_cmd_df();
    return;
  }
  
  // LS <path>
  if (strncmp(cmd, "LS ", 3) == 0) {
    assets_cmd_ls(cmd + 3);
    return;
  }
  if (strcmp(cmd, "LS") == 0) {
    assets_cmd_ls("/");
    return;
  }
  
  // STAT <path>
  if (strncmp(cmd, "STAT ", 5) == 0) {
    assets_cmd_stat(cmd + 5);
    return;
  }
  
  // CAT <path>
  if (strncmp(cmd, "CAT ", 4) == 0) {
    assets_cmd_cat(cmd + 4);
    return;
  }
  
  // MANIFEST <type>
  if (strncmp(cmd, "MANIFEST ", 9) == 0) {
    assets_cmd_manifest(cmd + 9);
    return;
  }
  if (strcmp(cmd, "MANIFEST") == 0) {
    assets_cmd_manifest("scenes");  // Default to scenes
    return;
  }
  
  // MKDIR <path>
  if (strncmp(cmd, "MKDIR ", 6) == 0) {
    assets_cmd_mkdir(cmd + 6);
    return;
  }
  
  // RM <path>
  if (strncmp(cmd, "RM ", 3) == 0) {
    assets_cmd_rm(cmd + 3);
    return;
  }
  
  // MV <src> <dst>
  if (strncmp(cmd, "MV ", 3) == 0) {
    const char *args = cmd + 3;
    char src[MAX_PATH_LEN], dst[MAX_PATH_LEN];
    if (sscanf(args, "%255s %255s", src, dst) == 2) {
      assets_cmd_mv(src, dst);
    } else {
      send_response("ERROR: Usage: MV <src> <dst>");
    }
    return;
  }
  
  // CP <src> <dst>
  if (strncmp(cmd, "CP ", 3) == 0) {
    const char *args = cmd + 3;
    char src[MAX_PATH_LEN], dst[MAX_PATH_LEN];
    if (sscanf(args, "%255s %255s", src, dst) == 2) {
      assets_cmd_cp(src, dst);
    } else {
      send_response("ERROR: Usage: CP <src> <dst>");
    }
    return;
  }
  
  // PUT <path> <size>
  if (strncmp(cmd, "PUT ", 4) == 0) {
    const char *args = cmd + 4;
    char path[MAX_PATH_LEN];
    size_t size = 0;
    if (sscanf(args, "%255s %zu", path, &size) == 2) {
      assets_cmd_put(path, size);
    } else {
      send_response("ERROR: Usage: PUT <path> <size>");
    }
    return;
  }
  
  // GET <path>
  if (strncmp(cmd, "GET ", 4) == 0) {
    assets_cmd_get(cmd + 4);
    return;
  }
  
  // ZIP <path>
  if (strncmp(cmd, "ZIP ", 4) == 0) {
    assets_cmd_zip(cmd + 4);
    return;
  }
  
  // Unknown command
  send_response("ERROR: Unknown assets command");
}

// LS - list directory
static void assets_cmd_ls(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  DIR *dir = opendir(full_path);
  if (!dir) {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot open directory: %s", path);
    send_response(resp);
    return;
  }
  
  // Build JSON array response
  char response[2048] = "[";
  size_t pos = 1;
  bool first = true;
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    
    // Get file info - entry_path needs extra room for d_name
    char entry_path[MAX_PATH_LEN + 256];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", full_path, entry->d_name);
    
    struct stat st;
    if (stat(entry_path, &st) != 0) continue;
    
    const char *type = S_ISDIR(st.st_mode) ? "dir" : "file";
    
    // Add comma if not first
    if (!first && pos < sizeof(response) - 100) {
      response[pos++] = ',';
    }
    first = false;
    
    // Add entry JSON
    int written = snprintf(response + pos, sizeof(response) - pos,
      "{\"name\":\"%s\",\"type\":\"%s\",\"size\":%ld}",
      entry->d_name, type, (long)st.st_size);
    
    if (written > 0 && pos + written < sizeof(response) - 10) {
      pos += written;
    }
  }
  
  closedir(dir);
  
  // Close array
  if (pos < sizeof(response) - 2) {
    response[pos++] = ']';
    response[pos] = '\0';
  }
  
  send_response(response);
}

// STAT - get file info
static void assets_cmd_stat(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  struct stat st;
  if (stat(full_path, &st) != 0) {
    send_response("ERROR: File not found");
    return;
  }
  
  const char *type = S_ISDIR(st.st_mode) ? "dir" : "file";
  char response[128];
  snprintf(response, sizeof(response), 
    "{\"size\":%ld,\"type\":\"%s\"}", (long)st.st_size, type);
  send_response(response);
}

// DF - filesystem stats
static void assets_cmd_df(void) {
  size_t total = 0, used = 0;
  esp_err_t err = esp_littlefs_info("assets", &total, &used);
  
  if (err != ESP_OK) {
    send_response("ERROR: Cannot get filesystem info");
    return;
  }
  
  char response[128];
  snprintf(response, sizeof(response),
    "{\"total\":%u,\"used\":%u,\"free\":%u}",
    (unsigned)total, (unsigned)used, (unsigned)(total - used));
  send_response(response);
}

// CAT - read small text file
static void assets_cmd_cat(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  struct stat st;
  if (stat(full_path, &st) != 0) {
    send_response("ERROR: File not found");
    return;
  }
  
  if (st.st_size > CAT_MAX_SIZE) {
    send_response("ERROR: File too large for CAT (use GET)");
    return;
  }
  
  FILE *f = fopen(full_path, "r");
  if (!f) {
    send_response("ERROR: Cannot open file");
    return;
  }
  
  // Read entire file
  char *buf = malloc(st.st_size + 1);
  if (!buf) {
    fclose(f);
    send_response("ERROR: Out of memory");
    return;
  }
  
  size_t read_bytes = fread(buf, 1, st.st_size, f);
  fclose(f);
  buf[read_bytes] = '\0';
  
  // Send content using chunked send_response for reliable delivery
  send_response(buf);
  
  free(buf);
}

// MKDIR - create directory
static void assets_cmd_mkdir(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  if (mkdir(full_path, 0755) == 0) {
    send_response("OK");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot create directory: %s", strerror(errno));
    send_response(resp);
  }
}

// RM - remove file or empty directory
static void assets_cmd_rm(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  struct stat st;
  if (stat(full_path, &st) != 0) {
    send_response("ERROR: File not found");
    return;
  }
  
  int result;
  if (S_ISDIR(st.st_mode)) {
    result = rmdir(full_path);
  } else {
    result = unlink(full_path);
  }
  
  if (result == 0) {
    // Trigger manifest update if needed
    assets_file_deleted(full_path);
    send_response("OK");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot remove: %s", strerror(errno));
    send_response(resp);
  }
}

// MV - move/rename
static void assets_cmd_mv(const char *src, const char *dst) {
  char full_src[MAX_PATH_LEN], full_dst[MAX_PATH_LEN];
  build_full_path(full_src, sizeof(full_src), src);
  build_full_path(full_dst, sizeof(full_dst), dst);
  
  if (rename(full_src, full_dst) == 0) {
    // Trigger manifest updates
    assets_file_deleted(full_src);
    assets_file_created(full_dst);
    send_response("OK");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot move: %s", strerror(errno));
    send_response(resp);
  }
}

// CP - copy file
static void assets_cmd_cp(const char *src, const char *dst) {
  char full_src[MAX_PATH_LEN], full_dst[MAX_PATH_LEN];
  build_full_path(full_src, sizeof(full_src), src);
  build_full_path(full_dst, sizeof(full_dst), dst);
  
  FILE *fsrc = fopen(full_src, "rb");
  if (!fsrc) {
    send_response("ERROR: Cannot open source file");
    return;
  }
  
  FILE *fdst = fopen(full_dst, "wb");
  if (!fdst) {
    fclose(fsrc);
    send_response("ERROR: Cannot create destination file");
    return;
  }
  
  char buf[512];
  size_t bytes;
  while ((bytes = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
    if (fwrite(buf, 1, bytes, fdst) != bytes) {
      fclose(fsrc);
      fclose(fdst);
      unlink(full_dst);
      send_response("ERROR: Write failed");
      return;
    }
  }
  
  fclose(fsrc);
  fclose(fdst);
  
  // Trigger manifest update
  assets_file_created(full_dst);
  send_response("OK");
}

// PUT - receive file upload
static void assets_cmd_put(const char *path, size_t size) {
  if (size == 0 || size > 16 * 1024 * 1024) {  // Max 16MB
    send_response("ERROR: Invalid file size");
    return;
  }
  
  build_full_path(s_assets_path, sizeof(s_assets_path), path);
  
  // Open file for writing
  s_assets_file = fopen(s_assets_path, "wb");
  if (!s_assets_file) {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot create file: %s", strerror(errno));
    send_response(resp);
    return;
  }
  
  s_assets_file_size = size;
  s_assets_bytes_transferred = 0;
  s_state = CDC_STATE_ASSETS_RECEIVING;
  
  ESP_LOGI(TAG, "Receiving file: %s (%u bytes)", s_assets_path, (unsigned)size);
  send_response("READY");
}

// GET - send file download
static void assets_cmd_get(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);
  
  struct stat st;
  if (stat(full_path, &st) != 0) {
    send_response("ERROR: File not found");
    return;
  }
  
  if (S_ISDIR(st.st_mode)) {
    send_response("ERROR: Cannot GET directory");
    return;
  }
  
  s_assets_file = fopen(full_path, "rb");
  if (!s_assets_file) {
    send_response("ERROR: Cannot open file");
    return;
  }
  
  strncpy(s_assets_path, full_path, sizeof(s_assets_path) - 1);
  s_assets_file_size = st.st_size;
  s_assets_bytes_transferred = 0;
  
  // Send size response, then start sending data
  char resp[64];
  snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)st.st_size);
  send_response(resp);
  
  s_state = CDC_STATE_ASSETS_SENDING;
  ESP_LOGI(TAG, "Sending file: %s (%u bytes)", full_path, (unsigned)st.st_size);
}

// Handle file send in assets mode (called from main task loop)
static void handle_assets_send(void) {
  if (!s_assets_file || s_state != CDC_STATE_ASSETS_SENDING) return;
  
  // Send chunks
  uint8_t buf[512];
  size_t remaining = s_assets_file_size - s_assets_bytes_transferred;
  size_t to_read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
  
  size_t bytes = fread(buf, 1, to_read, s_assets_file);
  if (bytes > 0) {
    send_binary(buf, bytes);
    s_assets_bytes_transferred += bytes;
  }
  
  // Check if done
  if (s_assets_bytes_transferred >= s_assets_file_size || bytes == 0) {
    fclose(s_assets_file);
    s_assets_file = NULL;
    s_state = CDC_STATE_ASSETS;
    ESP_LOGI(TAG, "File send complete: %u bytes", (unsigned)s_assets_bytes_transferred);
  }
}

// Handle binary data for file upload
static void handle_assets_binary_data(const uint8_t *data, size_t len) {
  if (!s_assets_file) {
    ESP_LOGE(TAG, "No file open for writing");
    s_state = CDC_STATE_ASSETS;
    send_response("ERROR: No file open");
    return;
  }
  
  size_t remaining = s_assets_file_size - s_assets_bytes_transferred;
  size_t to_write = (len > remaining) ? remaining : len;
  
  size_t written = fwrite(data, 1, to_write, s_assets_file);
  s_assets_bytes_transferred += written;
  
  if (written != to_write) {
    ESP_LOGE(TAG, "Write error at %u bytes", (unsigned)s_assets_bytes_transferred);
    fclose(s_assets_file);
    s_assets_file = NULL;
    unlink(s_assets_path);
    s_state = CDC_STATE_ASSETS;
    send_response("ERROR: Write failed");
    return;
  }
  
  // Check if done
  if (s_assets_bytes_transferred >= s_assets_file_size) {
    fclose(s_assets_file);
    s_assets_file = NULL;
    
    ESP_LOGI(TAG, "File receive complete: %s (%u bytes)", 
      s_assets_path, (unsigned)s_assets_bytes_transferred);
    
    // Trigger manifest update
    assets_file_created(s_assets_path);
    
    s_state = CDC_STATE_ASSETS;
    send_response("OK");
  }
}

// MANIFEST - get manifest for a folder type
static void assets_cmd_manifest(const char *type) {
  char manifest_path[MAX_PATH_LEN];
  
  if (strcmp(type, "scenes") == 0) {
    snprintf(manifest_path, sizeof(manifest_path), "%s/scenes/manifest.json", ASSETS_BASE_PATH);
  } else if (strcmp(type, "devices") == 0) {
    snprintf(manifest_path, sizeof(manifest_path), "%s/devices/manifest.json", ASSETS_BASE_PATH);
  } else if (strcmp(type, "images") == 0) {
    snprintf(manifest_path, sizeof(manifest_path), "%s/images/manifest.json", ASSETS_BASE_PATH);
  } else {
    send_response("ERROR: Unknown manifest type (use: scenes, devices, images)");
    return;
  }
  
  // Read and send manifest file
  FILE *f = fopen(manifest_path, "r");
  if (!f) {
    send_response("ERROR: Manifest not found");
    return;
  }
  
  struct stat st;
  if (stat(manifest_path, &st) != 0 || st.st_size > 8192) {
    fclose(f);
    send_response("ERROR: Manifest too large or inaccessible");
    return;
  }
  
  char *buf = malloc(st.st_size + 1);
  if (!buf) {
    fclose(f);
    send_response("ERROR: Out of memory");
    return;
  }
  
  size_t read_bytes = fread(buf, 1, st.st_size, f);
  fclose(f);
  buf[read_bytes] = '\0';
  
  // Send manifest content using chunked send_response
  send_response(buf);
  
  free(buf);
}

// ============================================================================
// ZIP Archive Support (runs in dedicated task with large stack)
// ============================================================================

// Custom allocator for miniz to use PSRAM
static void *psram_alloc(void *opaque, size_t items, size_t size) {
  (void)opaque;
  return heap_caps_malloc(items * size, MALLOC_CAP_SPIRAM);
}

static void psram_free(void *opaque, void *address) {
  (void)opaque;
  heap_caps_free(address);
}

static void *psram_realloc(void *opaque, void *address, size_t items, size_t size) {
  (void)opaque;
  return heap_caps_realloc(address, items * size, MALLOC_CAP_SPIRAM);
}

// Recursively add files from a directory to a ZIP archive
// base_path: full filesystem path to directory being archived
// archive_prefix: prefix to use in archive (empty string for root)
static bool zip_add_directory(mz_zip_archive *zip, const char *base_path, const char *archive_prefix) {
  DIR *dir = opendir(base_path);
  if (!dir) {
    ESP_LOGW(TAG, "ZIP: Cannot open directory %s", base_path);
    return false;
  }
  
  // Use heap for path buffers to reduce stack usage
  char *full_path = malloc(MAX_PATH_LEN + 256);
  char *archive_name = malloc(MAX_PATH_LEN);
  if (!full_path || !archive_name) {
    if (full_path) free(full_path);
    if (archive_name) free(archive_name);
    closedir(dir);
    return false;
  }
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip . and .. and hidden files
    if (entry->d_name[0] == '.') continue;
    
    snprintf(full_path, MAX_PATH_LEN + 256, "%s/%s", base_path, entry->d_name);
    
    struct stat st;
    if (stat(full_path, &st) != 0) continue;
    
    if (S_ISDIR(st.st_mode)) {
      // Build archive name for subdirectory
      if (archive_prefix[0]) {
        snprintf(archive_name, MAX_PATH_LEN, "%s/%s", archive_prefix, entry->d_name);
      } else {
        snprintf(archive_name, MAX_PATH_LEN, "%s", entry->d_name);
      }
      
      // Recurse into subdirectory
      if (!zip_add_directory(zip, full_path, archive_name)) {
        free(full_path);
        free(archive_name);
        closedir(dir);
        return false;
      }
    } else {
      // Regular file - add to archive
      if (archive_prefix[0]) {
        snprintf(archive_name, MAX_PATH_LEN, "%s/%s", archive_prefix, entry->d_name);
      } else {
        snprintf(archive_name, MAX_PATH_LEN, "%s", entry->d_name);
      }
      
      // Read file contents
      FILE *f = fopen(full_path, "rb");
      if (!f) {
        ESP_LOGW(TAG, "ZIP: Cannot open file %s", full_path);
        continue;
      }
      
      // Allocate buffer for file
      char *file_buf = malloc(st.st_size);
      if (!file_buf) {
        fclose(f);
        ESP_LOGW(TAG, "ZIP: Cannot allocate %ld bytes for %s", (long)st.st_size, entry->d_name);
        continue;
      }
      
      size_t read_bytes = fread(file_buf, 1, st.st_size, f);
      fclose(f);
      
      // Add to archive with NO compression (keeps stack usage low)
      if (!mz_zip_writer_add_mem(zip, archive_name, file_buf, read_bytes, MZ_NO_COMPRESSION)) {
        ESP_LOGW(TAG, "ZIP: Failed to add %s: %s", archive_name, 
                 mz_zip_get_error_string(mz_zip_get_last_error(zip)));
        free(file_buf);
        continue;
      }
      
      ESP_LOGD(TAG, "ZIP: Added %s (%u bytes)", archive_name, (unsigned)read_bytes);
      free(file_buf);
    }
  }
  
  free(full_path);
  free(archive_name);
  closedir(dir);
  return true;
}

// ZIP task - runs with large stack, deletes itself when done
static void zip_task(void *arg) {
  const char *full_path = (const char *)arg;
  
  ESP_LOGI(TAG, "ZIP task started for %s", full_path);
  
  // Initialize ZIP writer with PSRAM allocator
  mz_zip_archive zip;
  memset(&zip, 0, sizeof(zip));
  zip.m_pAlloc = psram_alloc;
  zip.m_pFree = psram_free;
  zip.m_pRealloc = psram_realloc;
  
  // Start with 64KB initial allocation, will grow as needed
  if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) {
    send_response("ERROR: Failed to initialize ZIP archive");
    goto cleanup;
  }
  
  // Recursively add directory contents (empty prefix = files at root of archive)
  if (!zip_add_directory(&zip, full_path, "")) {
    mz_zip_writer_end(&zip);
    send_response("ERROR: Failed to add files to archive");
    goto cleanup;
  }
  
  // Finalize the archive
  void *archive_buf = NULL;
  size_t archive_size = 0;
  
  if (!mz_zip_writer_finalize_heap_archive(&zip, &archive_buf, &archive_size)) {
    mz_zip_writer_end(&zip);
    send_response("ERROR: Failed to finalize archive");
    goto cleanup;
  }
  
  ESP_LOGI(TAG, "ZIP archive created: %u bytes", (unsigned)archive_size);
  
  // Send size response
  char resp[64];
  snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)archive_size);
  send_response(resp);
  
  // Stream the archive data
  send_binary((const uint8_t *)archive_buf, archive_size);
  
  // Cleanup miniz (also frees archive_buf since we used heap mode)
  mz_zip_writer_end(&zip);
  
  ESP_LOGI(TAG, "ZIP archive sent successfully");

cleanup:
  s_zip_in_progress = false;
  s_zip_task_handle = NULL;
  vTaskDelete(NULL);  // Delete self
}

// ZIP - spawn dedicated task to create archive
static void assets_cmd_zip(const char *path) {
  // Check if ZIP already in progress
  if (s_zip_in_progress) {
    send_response("ERROR: ZIP operation already in progress");
    return;
  }
  
  // Build and validate path
  build_full_path(s_zip_path, sizeof(s_zip_path), path);
  
  struct stat st;
  if (stat(s_zip_path, &st) != 0) {
    send_response("ERROR: Path not found");
    return;
  }
  
  if (!S_ISDIR(st.st_mode)) {
    send_response("ERROR: ZIP requires a directory path");
    return;
  }
  
  ESP_LOGI(TAG, "Spawning ZIP task for %s", s_zip_path);
  s_zip_in_progress = true;
  
  // Task stack MUST be in internal RAM because LittleFS accesses flash,
  // which disables cache and makes PSRAM inaccessible.
  // With MZ_NO_COMPRESSION and heap-allocated buffers, 16KB should suffice.
  BaseType_t ret = xTaskCreate(
    zip_task,           // Task function
    "zip_task",         // Task name
    16384,              // Stack size (16KB internal RAM)
    (void *)s_zip_path, // Task parameter
    4,                  // Priority
    &s_zip_task_handle  // Task handle
  );
  
  if (ret != pdPASS) {
    s_zip_in_progress = false;
    s_zip_task_handle = NULL;
    ESP_LOGE(TAG, "Failed to create ZIP task (not enough internal RAM?)");
    send_response("ERROR: Failed to create ZIP task");
  }
  // Response will be sent by zip_task
}

