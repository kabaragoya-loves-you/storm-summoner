#include "usb_cdc_update.h"
#include "firmware_update.h"
#include "assets_file_ops.h"
#include "assets_manager.h"
#include "lvgl_stream.h"
#include "display_driver.h"
#include "event_bus.h"
#include "midi_in.h"
#include "device_config.h"
#include "app_settings.h"
#include "settings_registry.h"
#include "scene.h"
#include "scene_inspect.h"
#include "transport.h"
#include "tempo.h"
#include "config.h"
#include "action.h"
#include "ui.h"
#include "screensaver.h"
#include "cv.h"
#include "expression.h"
#include "midi_in_uart.h"
#include "midi_control.h"
#include "cJSON.h"
#include "version.h"
#include "task_monitor.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
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
#include "freertos/semphr.h"

// Suppress warnings from miniz header (unused static functions)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "miniz/miniz.h"
#pragma GCC diagnostic pop

#define TAG "USB_CDC_UPDATE"

// Push EVT:clock on each beat while playing (~2/s at 120 BPM). Set 0 if USB load/lag is an issue.
#define CDC_CLOCK_NOTIFY_ON_BEAT 1

#define CDC_RX_BUF_SIZE 1024
#define CDC_CMD_BUF_SIZE 512
#define LOG_BUF_SIZE 512
#define CDC_VFS_PATH "/dev/cdccon"
#define ASSETS_BASE_PATH "/assets"
#define MAX_PATH_LEN 320  // Must accommodate paths + d_name (255)
#define CAT_MAX_SIZE 4096
#define SCENE_INSPECT_TEXT_SIZE 2048
#define SCENE_JSON_MAX_BYTES (256 * 1024)

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
  CDC_STATE_DISPLAY,  // LVGL display streaming mode
  CDC_STATE_MIDI_RELAY, // MIDI IN relay mode
  CDC_STATE_SETTINGS, // NVS settings mode
  CDC_STATE_CONFIG,   // Semantic settings mode (via settings_registry)
  CDC_STATE_SCENES,   // Scene management mode
  CDC_STATE_PEDALS,   // Pedal database browsing mode
  CDC_STATE_SCENE_RECEIVING,  // SCENE_PUT binary payload (idle)
  CDC_STATE_ERROR
} cdc_update_state_t;

static cdc_update_state_t s_state = CDC_STATE_IDLE;
static bool s_initialized = false;
static bool s_logging_enabled = false;

#define CDC_LOGI(fmt, ...) do { if (s_logging_enabled) ESP_LOGI(TAG, fmt, ##__VA_ARGS__); } while (0)
#define CDC_LOGD(fmt, ...) do { if (s_logging_enabled) ESP_LOGD(TAG, fmt, ##__VA_ARGS__); } while (0)
static uint8_t *s_update_buffer = NULL;
static size_t s_update_size = 0;
static size_t s_received_bytes = 0;
static bool s_is_firmware = false;
static char s_pending_assets_checksum[9] = {0};  // 8 hex chars + null
static uint8_t s_last_progress_sent = 0;  // Track last progress % sent

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

// EXTRACT state (for receiving ZIP data)
static uint8_t *s_extract_buffer = NULL;  // PSRAM buffer for incoming ZIP
static size_t s_extract_size = 0;         // Size of received ZIP data
static char s_extract_dest[MAX_PATH_LEN]; // Destination path for extraction
static bool s_extract_mode = false;       // Whether in EXTRACT receive mode
static TaskHandle_t s_extract_task_handle = NULL;
static volatile bool s_extract_in_progress = false;

// MIDI relay state
static bool s_midi_relay_active = false;
static bool s_midi_relay_show_clock = false;

// Forward declarations for MIDI relay
static void midi_relay_event_handler(const event_t* event, void* context);
static void midi_relay_stop(void);

// Scene CDC notifications (idle / text-safe modes)
static void cdc_scene_changed_handler(const event_t *event, void *context);
static void cdc_scene_updated_handler(const event_t *event, void *context);
static void cdc_scene_reordered_handler(const event_t *event, void *context);
static void cdc_scene_list_changed_handler(const event_t *event, void *context);
static void cdc_push_clock_evt(void);
static void cdc_clock_transport_handler(const event_t *event, void *context);
static void cdc_clock_tempo_handler(const event_t *event, void *context);
static void cdc_clock_position_handler(const event_t *event, void *context);
#if CDC_CLOCK_NOTIFY_ON_BEAT
static void cdc_clock_beat_handler(const event_t *event, void *context);
#endif
static void cdc_clock_action_handler(const event_t *event, void *context);
static void cdc_send_scene_inspect(const char *arg);
static uint8_t cdc_resolve_scene_index(const char *arg, bool *is_position);
static void cdc_send_info_json(void);
static void cdc_send_mem_json(void);
static void cdc_connections_handler(const event_t *event, void *context);
static void cdc_add_connections_json(cJSON *root);
static bool cdc_connection_usb(void);
static bool cdc_connection_cv(void);
static bool cdc_connection_expression(void);
static bool cdc_connection_midi_in(void);
static void cdc_push_connections_evt(void);
static void cdc_send_scene_get(const char *arg);
static void cdc_cmd_scene_put(const char *args);
static void handle_scene_put_binary(const uint8_t *data, size_t len);
static uint8_t *s_scene_put_buffer = NULL;
static size_t s_scene_put_size = 0;
static size_t s_scene_put_received = 0;
static uint8_t s_scene_put_index = 0;

static SemaphoreHandle_t s_cdc_tx_mutex = NULL;
static StaticSemaphore_t s_cdc_tx_mutex_buf;
static TaskHandle_t s_cdc_task_handle = NULL;
static bool s_cdc_tx_armed = false;
static uint8_t s_cdc_response_depth = 0;

#define CDC_NOTIFY_QUEUE_LEN 8
#define CDC_NOTIFY_LINE_MAX 96
static char s_notify_queue[CDC_NOTIFY_QUEUE_LEN][CDC_NOTIFY_LINE_MAX];
static uint8_t s_notify_head = 0;
static uint8_t s_notify_tail = 0;

static bool cdc_tx_write_locked(const uint8_t *data, size_t len, int max_retries,
                                int delay_ms);
static void cdc_tx_flush_locked(void);
static bool cdc_send_line_locked(const char *msg, int body_max_retries,
                                 int body_delay_ms);
static void cdc_notify_enqueue(const char *msg);
static void cdc_flush_pending_notifies(void);
static void cdc_send_notify_line(const char *msg);
static bool cdc_may_push_notify(void);

static bool cdc_tx_write(const uint8_t *data, size_t len, int max_retries,
                         int delay_ms) {
  if (!s_cdc_tx_armed || !s_cdc_tx_mutex || !data || len == 0) return false;
  if (xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) return false;
  bool ok = cdc_tx_write_locked(data, len, max_retries, delay_ms);
  if (ok) cdc_tx_flush_locked();
  xSemaphoreGive(s_cdc_tx_mutex);
  return ok;
}

static bool cdc_tx_write_locked(const uint8_t *data, size_t len, int max_retries,
                                int delay_ms) {
  if (!tud_cdc_n_connected(0)) return false;

  size_t sent = 0;
  int retry_count = 0;

  while (sent < len) {
    if (!tud_cdc_n_connected(0)) return false;

    uint32_t written = tud_cdc_n_write(0, data + sent, len - sent);
    sent += written;

    if (written == 0) {
      tud_cdc_n_write_flush(0);
      retry_count++;
      if (retry_count >= max_retries) return false;
      if (!s_cdc_task_handle ||
          xTaskGetCurrentTaskHandle() != s_cdc_task_handle) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS((TickType_t)delay_ms));
    } else {
      retry_count = 0;
    }
  }

  return true;
}

static void cdc_tx_flush_locked(void) {
  tud_cdc_n_write_flush(0);
}

static bool cdc_send_line_locked(const char *msg, int body_max_retries,
                                 int body_delay_ms) {
  if (!msg) return false;
  size_t len = strlen(msg);
  if (len == 0) return false;

  char stack_buf[512];
  if (len + 1 <= sizeof(stack_buf)) {
    memcpy(stack_buf, msg, len);
    stack_buf[len] = '\n';
    return cdc_tx_write_locked((const uint8_t *)stack_buf, len + 1,
                             body_max_retries, body_delay_ms);
  }

  if (!cdc_tx_write_locked((const uint8_t *)msg, len, body_max_retries,
                           body_delay_ms)) {
    return false;
  }
  return cdc_tx_write_locked((const uint8_t *)"\n", 1, 100, 1);
}

static void cdc_console_echo_bytes(const uint8_t *data, size_t len) {
  if (!s_cdc_tx_armed || !data || len == 0 || !s_cdc_tx_mutex) return;
  if (xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) return;
  (void)cdc_tx_write_locked(data, len, 100, 1);
  cdc_tx_flush_locked();
  xSemaphoreGive(s_cdc_tx_mutex);
}

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
  if (!s_cdc_tx_armed || !tud_cdc_n_connected(0)) {
    errno = EIO;
    return -1;
  }

  const char *ptr = (const char *)data;
  char crlf_buf[LOG_BUF_SIZE];
  size_t out_len = 0;

  for (size_t i = 0; i < size && out_len + 2 < sizeof(crlf_buf); i++) {
    if (ptr[i] == '\n') crlf_buf[out_len++] = '\r';
    crlf_buf[out_len++] = ptr[i];
  }

  if (out_len == 0) return (ssize_t)size;

  if (!s_cdc_tx_mutex || xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) {
    errno = EIO;
    return -1;
  }

  bool ok = cdc_tx_write_locked((const uint8_t *)crlf_buf, out_len, 100, 5);
  if (ok) cdc_tx_flush_locked();
  xSemaphoreGive(s_cdc_tx_mutex);

  if (!ok) {
    errno = EIO;
    return -1;
  }

  return (ssize_t)size;
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
    CDC_LOGI("CDC VFS registered at %s", CDC_VFS_PATH);
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
static void process_settings_command(const char *cmd);
static void process_config_command(const char *cmd);
static void process_scenes_command(const char *cmd);
static void process_pedals_command(const char *cmd);
static void send_response(const char *msg);
static void send_binary(const uint8_t *data, size_t len);
static void handle_binary_data(const uint8_t *data, size_t len);
static void handle_assets_binary_data(const uint8_t *data, size_t len);
static void handle_assets_send(void);
static void console_send(const char *str);
static void console_send_prompt(void);
static esp_err_t cdc_scene_apply_if_pending(esp_err_t err);

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
static void assets_cmd_rmrf(const char *path);
static void assets_cmd_extract(const char *path, size_t size);
static void extract_task(void *arg);

// Log redirect function - sends to CDC when in console mode
static int cdc_log_vprintf(const char *fmt, va_list args) {
  if (!fmt) return 0;

  char buf[LOG_BUF_SIZE];
  int len = vsnprintf(buf, sizeof(buf), fmt, args);

  // Send to CDC if connected and in console mode
  if (s_log_redirect_active && tud_cdc_n_connected(0)) {
    size_t write_len = (len < LOG_BUF_SIZE) ? (size_t)len : LOG_BUF_SIZE - 1;
    (void)cdc_tx_write((const uint8_t *)buf, write_len, 100, 5);
  }

  // Write to original JTAG destination (use saved stdout if redirected)
  FILE *dest = s_saved_stdout ? s_saved_stdout : stdout;
  fputs(buf, dest);
  return len;
}

// CDC Update Task
static void cdc_update_task(void *arg) {
  CDC_LOGI("CDC update task started");
  while (1) {
    usb_cdc_task();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

esp_err_t usb_cdc_update_init(bool enable_logging) {
  if (s_initialized) {
    ESP_LOGW(TAG, "CDC update already initialized");
    return ESP_OK;
  }

  s_logging_enabled = enable_logging;

  CDC_LOGI("Initializing CDC update handler");

  s_cdc_tx_mutex = xSemaphoreCreateMutexStatic(&s_cdc_tx_mutex_buf);
  if (!s_cdc_tx_mutex) {
    ESP_LOGE(TAG, "Failed to create CDC TX mutex");
    return ESP_FAIL;
  }

  // Register VFS for console stdout redirect
  cdc_vfs_register();

  // CDC is initialized by the composite descriptor
  // No additional initialization needed - TinyUSB handles it

  // Create task to poll CDC
  // Stack size for printf/VFS operations in console mode
  // ZIP operations spawn their own task with larger stack
  xTaskCreate(cdc_update_task, "cdc_update", 8192, NULL, 5, &s_cdc_task_handle);

  event_bus_subscribe(EVENT_SCENE_CHANGED, cdc_scene_changed_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_UPDATED, cdc_scene_updated_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_REORDERED, cdc_scene_reordered_handler, NULL);
  event_bus_subscribe(EVENT_SCENE_LIST_CHANGED, cdc_scene_list_changed_handler, NULL);
  event_bus_subscribe(EVENT_TRANSPORT_STATE_CHANGED, cdc_clock_transport_handler, NULL);
  event_bus_subscribe(EVENT_TEMPO_CHANGED, cdc_clock_tempo_handler, NULL);
  event_bus_subscribe(EVENT_TRANSPORT_POSITION_CHANGED, cdc_clock_position_handler, NULL);
#if CDC_CLOCK_NOTIFY_ON_BEAT
  event_bus_subscribe_named(EVENT_BEAT, cdc_clock_beat_handler, NULL, "cdc.clock_beat");
#endif
  event_bus_subscribe(EVENT_ACTION_EXECUTED, cdc_clock_action_handler, NULL);
  if (event_bus_subscribe(EVENT_CONNECTIONS_CHANGED, cdc_connections_handler, NULL) != ESP_OK)
    ESP_LOGE(TAG, "Failed to subscribe for connection status updates");

  s_initialized = true;
  CDC_LOGI("CDC update handler initialized");
  return ESP_OK;
}

void usb_cdc_update_arm_tx(void) {
  s_cdc_tx_armed = true;
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
      CDC_LOGI("CDC disconnected, exiting console mode");
    }
    // If we were in assets mode and disconnected, cleanup
    if (s_state == CDC_STATE_ASSETS || s_state == CDC_STATE_ASSETS_RECEIVING || 
        s_state == CDC_STATE_ASSETS_SENDING) {
      if (s_assets_file) {
        fclose(s_assets_file);
        s_assets_file = NULL;
      }
      if (s_extract_buffer) {
        heap_caps_free(s_extract_buffer);
        s_extract_buffer = NULL;
      }
      s_extract_mode = false;
      s_extract_in_progress = false;
      // Note: extract_task will handle its own cleanup if still running
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting assets mode");
    }
    // If we were in display streaming mode and disconnected, cleanup
    if (s_state == CDC_STATE_DISPLAY) {
      lvgl_stream_stop();
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting display mode");
    }
    // If we were in MIDI relay mode and disconnected, cleanup
    if (s_state == CDC_STATE_MIDI_RELAY) {
      midi_relay_stop();
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting MIDI relay mode");
    }
    // If we were in settings mode and disconnected, cleanup
    if (s_state == CDC_STATE_SETTINGS) {
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting settings mode");
    }
    // If we were in config mode and disconnected, cleanup
    if (s_state == CDC_STATE_CONFIG) {
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting config mode");
    }
    // If we were in scenes mode and disconnected, cleanup
    if (s_state == CDC_STATE_SCENES) {
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting scenes mode");
    }
    // If we were in pedals mode and disconnected, cleanup
    if (s_state == CDC_STATE_PEDALS) {
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected, exiting pedals mode");
    }
    // If we were receiving firmware/assets and disconnected, cleanup
    if (s_state == CDC_STATE_SCENE_RECEIVING) {
      if (s_scene_put_buffer) {
        heap_caps_free(s_scene_put_buffer);
        s_scene_put_buffer = NULL;
      }
      s_state = CDC_STATE_IDLE;
      CDC_LOGI("CDC disconnected during SCENE_PUT, aborted");
    }
    if (s_state == CDC_STATE_RECEIVING_FIRMWARE ||
        s_state == CDC_STATE_RECEIVING_ASSETS ||
        s_state == CDC_STATE_WAITING_COMMIT) {
      ESP_LOGW(TAG, "CDC disconnected during update, cleaning up");
      if (s_update_buffer) {
        heap_caps_free(s_update_buffer);
        s_update_buffer = NULL;
      }
      s_update_size = 0;
      s_received_bytes = 0;
      s_pending_assets_checksum[0] = '\0';
      s_state = CDC_STATE_IDLE;
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
          if (ch == '\n') {
            const uint8_t crlf[] = { '\r', ch };
            cdc_console_echo_bytes(crlf, sizeof(crlf));
          } else {
            cdc_console_echo_bytes(&ch, 1);
          }
          
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
      } else if (s_state == CDC_STATE_SCENE_RECEIVING) {
        handle_scene_put_binary(buf, count);
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
      } else if (s_state == CDC_STATE_DISPLAY) {
        // In display mode, handle EXIT, SYNC, and STATS commands
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              if (strcmp(s_cmd_buffer, "EXIT") == 0 || strcmp(s_cmd_buffer, "exit") == 0) {
                CDC_LOGI("Exiting display mode");
                lvgl_stream_stop();
                s_state = CDC_STATE_IDLE;
                send_response("DISPLAY_STOPPED");
              } else if (strcmp(s_cmd_buffer, "SYNC") == 0 || strcmp(s_cmd_buffer, "sync") == 0) {
                // Request full screen redraw - no response to avoid contending with pixel stream
                lvgl_stream_request_sync();
              } else if (strcmp(s_cmd_buffer, "STATS") == 0 || strcmp(s_cmd_buffer, "stats") == 0) {
                // Return streaming statistics
                // Format: STATS <sent> <dropped> <bytes> <queue_now> <queue_max>
                uint32_t sent, dropped, bytes;
                lvgl_stream_get_stats(&sent, &dropped, &bytes);
                uint32_t queue_now = lvgl_stream_get_current_queue_depth();
                uint32_t queue_max = lvgl_stream_get_max_queue_depth();
                char resp[128];
                snprintf(resp, sizeof(resp), "STATS %u %u %u %u %u",
                  (unsigned)sent, (unsigned)dropped, (unsigned)bytes,
                  (unsigned)queue_now, (unsigned)queue_max);
                send_response(resp);
              }
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_MIDI_RELAY) {
        // In MIDI relay mode, only handle EXIT command
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              if (strcmp(s_cmd_buffer, "EXIT") == 0 || strcmp(s_cmd_buffer, "exit") == 0) {
                CDC_LOGI("Exiting MIDI relay mode");
                midi_relay_stop();
                s_state = CDC_STATE_IDLE;
                send_response("MIDI_STOPPED");
              }
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_SETTINGS) {
        // In settings mode, parse commands (text mode, no echo)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_settings_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_CONFIG) {
        // In config mode, parse commands (text mode, no echo)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_config_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_SCENES) {
        // In scenes mode, parse commands (text mode, no echo)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_scenes_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      } else if (s_state == CDC_STATE_PEDALS) {
        // In pedals mode, parse commands (text mode, no echo)
        for (uint32_t i = 0; i < count; i++) {
          if (buf[i] == '\n' || buf[i] == '\r') {
            if (s_cmd_pos > 0) {
              s_cmd_buffer[s_cmd_pos] = '\0';
              process_pedals_command(s_cmd_buffer);
              s_cmd_pos = 0;
            }
          } else if (s_cmd_pos < CDC_CMD_BUF_SIZE - 1) {
            s_cmd_buffer[s_cmd_pos++] = buf[i];
          }
        }
      }
    }
  }

  cdc_flush_pending_notifies();
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
  if (!s_cdc_tx_armed) return;
  if (!tud_cdc_n_connected(0)) {
    ESP_LOGW(TAG, "send_response: CDC not connected, dropping '%s'", msg);
    return;
  }
  if (!s_cdc_tx_mutex || xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) return;

  s_cdc_response_depth++;
  if (!cdc_send_line_locked(msg, 100, 5)) {
    ESP_LOGW(TAG, "send_response: failed to send '%s'", msg);
  } else {
    cdc_tx_flush_locked();
  }
  s_cdc_response_depth--;
  xSemaphoreGive(s_cdc_tx_mutex);
}

static void cdc_notify_enqueue(const char *msg) {
  if (!msg || !msg[0]) return;
  size_t len = strlen(msg);
  if (len >= CDC_NOTIFY_LINE_MAX) return;

  uint8_t next = (uint8_t)((s_notify_head + 1) % CDC_NOTIFY_QUEUE_LEN);
  if (next == s_notify_tail) return;

  memcpy(s_notify_queue[s_notify_head], msg, len + 1);
  s_notify_head = next;
}

static void cdc_send_notify_line(const char *msg) {
  if (!msg || !msg[0]) return;
  cdc_notify_enqueue(msg);
}

static void cdc_flush_pending_notifies(void) {
  if (!s_cdc_tx_armed || !tud_cdc_n_connected(0)) return;
  while (s_notify_tail != s_notify_head) {
    if (!cdc_may_push_notify()) break;
    if (!s_cdc_tx_mutex || xSemaphoreTake(s_cdc_tx_mutex, 0) != pdTRUE) break;
    bool ok = cdc_send_line_locked(s_notify_queue[s_notify_tail], 1, 0);
    if (ok) cdc_tx_flush_locked();
    xSemaphoreGive(s_cdc_tx_mutex);
    if (!ok) break;
    s_notify_tail = (uint8_t)((s_notify_tail + 1) % CDC_NOTIFY_QUEUE_LEN);
  }
}

static void send_json_response(const char *msg) {
  if (!s_cdc_tx_armed) return;
  if (!tud_cdc_n_connected(0)) {
    ESP_LOGW(TAG, "send_json_response: CDC not connected");
    return;
  }
  if (!msg || !s_cdc_tx_mutex ||
      xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  size_t len = strlen(msg);
  int max_retries = 500 + (int)(len / 64);
  bool ok = true;

  s_cdc_response_depth++;
  if (!cdc_tx_write_locked((const uint8_t *)msg, len, max_retries, 2)) {
    ok = false;
  } else if (!cdc_tx_write_locked((const uint8_t *)"\n", 1, 100, 1)) {
    ok = false;
  }

  if (ok) {
    cdc_tx_flush_locked();
  } else {
    ESP_LOGW(TAG, "send_json_response: max retries, sent partial (avail=%u)",
      (unsigned)tud_cdc_n_write_available(0));
  }

  s_cdc_response_depth--;
  xSemaphoreGive(s_cdc_tx_mutex);
}

static void send_binary(const uint8_t *data, size_t len) {
  if (!s_cdc_tx_armed) return;
  if (!tud_cdc_n_connected(0) || !data || len == 0) return;
  if (!s_cdc_tx_mutex || xSemaphoreTake(s_cdc_tx_mutex, portMAX_DELAY) != pdTRUE) return;

  const size_t chunk_size = 512;
  const int max_retries = 100;
  size_t sent = 0;

  s_cdc_response_depth++;
  while (sent < len) {
    if (!tud_cdc_n_connected(0)) {
      ESP_LOGW(TAG, "CDC disconnected during send_binary");
      break;
    }

    size_t to_send = (len - sent > chunk_size) ? chunk_size : len - sent;
    int retry_count = 0;
    bool chunk_ok = false;

    while (!chunk_ok) {
      if (!tud_cdc_n_connected(0)) break;

      uint32_t written = tud_cdc_n_write(0, data + sent, to_send);
      if (written > 0) {
        sent += written;
        chunk_ok = true;
        continue;
      }

      tud_cdc_n_write_flush(0);
      retry_count++;
      if (retry_count >= max_retries) {
        ESP_LOGW(TAG, "send_binary: max retries reached, sent %u/%u bytes",
          (unsigned)sent, (unsigned)len);
        goto send_binary_done;
      }
      if (!s_cdc_task_handle ||
          xTaskGetCurrentTaskHandle() != s_cdc_task_handle) {
        goto send_binary_done;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }

send_binary_done:
  cdc_tx_flush_locked();
  s_cdc_response_depth--;
  xSemaphoreGive(s_cdc_tx_mutex);
}

static bool cdc_may_push_notify(void) {
  if (!tud_cdc_n_connected(0)) return false;
  if (s_cdc_response_depth > 0) return false;

  switch (s_state) {
    case CDC_STATE_IDLE:
    case CDC_STATE_CONSOLE:
    case CDC_STATE_SCENES:
    case CDC_STATE_CONFIG:
    case CDC_STATE_SETTINGS:
    case CDC_STATE_PEDALS:
      return true;
    default:
      return false;
  }
}

static void cdc_push_scene_evt(const char *kind, uint8_t scene_index) {
  char buf[48];
  snprintf(buf, sizeof(buf), "EVT:%s:%u", kind, (unsigned)scene_index);
  CDC_LOGI("CDC notify: %s", buf);
  cdc_send_notify_line(buf);
}

void usb_cdc_notify_programming(bool active) {
  char buf[24];
  snprintf(buf, sizeof(buf), "EVT:programming:%u", active ? 1u : 0u);
  CDC_LOGI("CDC notify: %s", buf);
  cdc_send_notify_line(buf);
}

static void cdc_scene_changed_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_SCENE_CHANGED) return;
  cdc_push_scene_evt("scene_changed", event->data.value_uint8);
  cdc_push_clock_evt();
}

static void cdc_scene_updated_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_SCENE_UPDATED) return;
  cdc_push_scene_evt("scene_updated", event->data.value_uint8);
}

static void cdc_scene_reordered_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_SCENE_REORDERED) return;
  cdc_push_scene_evt("scene_reordered", event->data.value_uint8);
}

static void cdc_scene_list_changed_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_SCENE_LIST_CHANGED) return;
  cdc_push_scene_evt("scene_list_changed", event->data.value_uint8);
}

static void cdc_send_scene_inspect(const char *arg) {
  bool is_position = false;
  uint8_t scene_index;

  if (!arg || !arg[0]) {
    scene_index = scene_get_current_index();
  } else {
    scene_index = cdc_resolve_scene_index(arg, &is_position);
    if (is_position) {
      unsigned long pos = strtoul(arg, NULL, 10);
      if (pos >= scene_get_total_count()) {
        send_response("ERROR: Invalid scene position");
        return;
      }
    }
    if (!scene_index_in_manifest(scene_index)) {
      send_response("ERROR: Scene not found");
      return;
    }
  }

  CDC_LOGI("SCENE_INSPECT begin (index=%u)", (unsigned)scene_index);
  char *text_buf = heap_caps_malloc(SCENE_INSPECT_TEXT_SIZE, MALLOC_CAP_SPIRAM);
  if (!text_buf) {
    send_response("ERROR: Out of memory");
    return;
  }

  bool truncated = false;
  esp_err_t err = scene_inspect_at_index(scene_index, text_buf,
    SCENE_INSPECT_TEXT_SIZE, &truncated);
  if (err != ESP_OK) {
    heap_caps_free(text_buf);
    if (err == ESP_ERR_NOT_FOUND) {
      send_response("ERROR: Scene not found");
    } else {
      send_response("ERROR: Failed to build inspect");
    }
    return;
  }

  CDC_LOGI("SCENE_INSPECT built (truncated=%d)", truncated ? 1 : 0);

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    heap_caps_free(text_buf);
    send_response("ERROR: Out of memory");
    return;
  }

  cJSON_AddStringToObject(root, "text", text_buf);
  cJSON_AddBoolToObject(root, "truncated", truncated);
  cJSON_AddBoolToObject(root, "midi_control_enabled", midi_control_is_enabled());

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  heap_caps_free(text_buf);

  if (json) {
    CDC_LOGI("SCENE_INSPECT sending %u bytes", (unsigned)strlen(json));
    send_response(json);
    cJSON_free(json);
    CDC_LOGI("SCENE_INSPECT done");
  } else {
    send_response("ERROR: Out of memory");
  }
}

static const char *cdc_trs_type_str(midi_trs_type_t trs) {
  switch (trs) {
    case MIDI_TRS_TYPE_A: return "TYPE_A";
    case MIDI_TRS_TYPE_B: return "TYPE_B";
    case MIDI_TRS_TYPE_TS: return "TYPE_TS";
    case MIDI_TRS_TYPE_BOTH: return "BOTH";
    default: return "unknown";
  }
}

static bool cdc_parse_trs_type_str(const char *str, midi_trs_type_t *out) {
  if (!str || !out) return false;
  if (strcmp(str, "TYPE_A") == 0) {
    *out = MIDI_TRS_TYPE_A;
    return true;
  }
  if (strcmp(str, "TYPE_B") == 0) {
    *out = MIDI_TRS_TYPE_B;
    return true;
  }
  if (strcmp(str, "TYPE_TS") == 0) {
    *out = MIDI_TRS_TYPE_TS;
    return true;
  }
  if (strcmp(str, "BOTH") == 0) {
    *out = MIDI_TRS_TYPE_BOTH;
    return true;
  }
  return false;
}

static bool cdc_try_pedal_set_command(const char *cmd) {
  if (strncmp(cmd, "PEDAL_SET ", 10) != 0) return false;

  const char *args = cmd + 10;
  while (*args == ' ') args++;

  if (strncmp(args, "CHANNEL ", 8) == 0) {
    unsigned ch = strtoul(args + 8, NULL, 10);
    if (ch < 1 || ch > 16) {
      send_response("ERROR: Channel must be 1-16");
      return true;
    }
    esp_err_t err = device_config_set_channel((uint8_t)ch);
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return true;
  }

  if (strncmp(args, "TRS ", 4) == 0) {
    const char *trs_str = args + 4;
    while (*trs_str == ' ') trs_str++;
    midi_trs_type_t trs;
    if (!cdc_parse_trs_type_str(trs_str, &trs)) {
      send_response("ERROR: Invalid TRS type");
      return true;
    }
    esp_err_t err = device_config_set_trs_type(trs);
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return true;
  }

  if (strncmp(args, "SEND_CLOCK ", 11) == 0) {
    const char *val = args + 11;
    while (*val == ' ') val++;
    bool send;
    if (strcmp(val, "1") == 0) {
      send = true;
    } else if (strcmp(val, "0") == 0) {
      send = false;
    } else {
      send_response("ERROR: Send clock must be 0 or 1");
      return true;
    }
    esp_err_t err = device_config_set_send_clock(send);
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return true;
  }

  send_response("ERROR: Usage PEDAL_SET CHANNEL|TRS|SEND_CLOCK <value>");
  return true;
}

static const char *cdc_scene_mode_str(scene_mode_t mode) {
  switch (mode) {
    case SCENE_MODE_PRESET_SYNC: return "preset_sync";
    case SCENE_MODE_ADVANCED: return "advanced";
    default: return "single";
  }
}

static const char *cdc_bank_mode_str(bank_select_mode_t mode) {
  switch (mode) {
    case BANK_SELECT_CC0: return "CC0";
    case BANK_SELECT_CC0_CC32: return "CC0_CC32";
    default: return "none";
  }
}

typedef struct {
  uint16_t bpm;
  uint8_t playing;
  uint32_t bar;
  uint8_t beat;
  uint8_t numerator;
  uint8_t denominator;
  uint8_t use_transport;
  uint8_t flag_enabled;
  uint8_t flag;
} cdc_clock_snapshot_t;

static cdc_clock_snapshot_t s_last_clock_notify;

static void cdc_read_clock_snapshot(cdc_clock_snapshot_t *out) {
  if (!out) return;

  time_signature_t sig = tempo_get_time_signature();
  uint8_t scene_idx = scene_get_current_index();

  out->bpm = tempo_get_bpm();
  out->use_transport = scene_get_use_transport(scene_idx) ? 1u : 0u;
  out->playing = transport_is_playing() ? 1u : 0u;
  if (out->use_transport) {
    out->bar = transport_get_current_bar();
    out->beat = transport_get_current_beat();
  } else {
    // Free-running scenes advance tempo's beat counter without transport play.
    out->bar = 1;
    out->beat = tempo_get_current_beat();
  }
  if (out->beat == 0) out->beat = 1;
  out->numerator = sig.numerator ? sig.numerator : 4;
  out->denominator = sig.denominator ? sig.denominator : 4;
  out->flag_enabled = config_get_flag_enabled() ? 1u : 0u;
  out->flag = action_get_flag() ? 1u : 0u;
}

static void cdc_add_clock_json(cJSON *root) {
  if (!root) return;

  cdc_clock_snapshot_t snap;
  cdc_read_clock_snapshot(&snap);

  cJSON *clock = cJSON_CreateObject();
  if (!clock) return;

  cJSON_AddNumberToObject(clock, "bpm", (unsigned)snap.bpm);
  cJSON_AddStringToObject(clock, "transport",
    snap.playing ? "playing" : "stopped");
  cJSON_AddNumberToObject(clock, "bar", (unsigned)snap.bar);
  cJSON_AddNumberToObject(clock, "beat", (unsigned)snap.beat);
  cJSON_AddBoolToObject(clock, "use_transport", snap.use_transport != 0);

  cJSON *ts = cJSON_CreateObject();
  if (ts) {
    cJSON_AddNumberToObject(ts, "numerator", (unsigned)snap.numerator);
    cJSON_AddNumberToObject(ts, "denominator", (unsigned)snap.denominator);
    cJSON_AddItemToObject(clock, "time_signature", ts);
  }

  cJSON_AddBoolToObject(clock, "flag_enabled", snap.flag_enabled != 0);
  cJSON_AddBoolToObject(clock, "flag", snap.flag != 0);

  cJSON_AddItemToObject(root, "clock", clock);
}

static void cdc_push_clock_evt(void) {
  cdc_clock_snapshot_t snap;
  cdc_read_clock_snapshot(&snap);

  if (memcmp(&snap, &s_last_clock_notify, sizeof(snap)) == 0)
    return;
  s_last_clock_notify = snap;

  char buf[96];
  snprintf(buf, sizeof(buf), "EVT:clock:%u:%u:%lu:%u:%u:%u:%u:%u:%u",
    (unsigned)snap.bpm, (unsigned)snap.playing, (unsigned long)snap.bar,
    (unsigned)snap.beat, (unsigned)snap.numerator, (unsigned)snap.denominator,
    (unsigned)snap.use_transport, (unsigned)snap.flag_enabled,
    (unsigned)snap.flag);
  cdc_send_notify_line(buf);
}

static void cdc_clock_transport_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_TRANSPORT_STATE_CHANGED) return;
  cdc_push_clock_evt();
}

static void cdc_clock_tempo_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_TEMPO_CHANGED) return;
  cdc_push_clock_evt();
}

static void cdc_clock_position_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_TRANSPORT_POSITION_CHANGED) return;
  cdc_push_clock_evt();
}

#if CDC_CLOCK_NOTIFY_ON_BEAT
static void cdc_clock_beat_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_BEAT) return;
  cdc_push_clock_evt();
}
#endif

static void cdc_clock_action_handler(const event_t *event, void *context) {
  (void)context;
  if (!event || event->type != EVENT_ACTION_EXECUTED) return;
  cdc_push_clock_evt();
}

extern bool input_get_cable_detection_enabled(void);

static const char *cdc_cv_range_str(cv_range_t range) {
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: return "+/-10V";
    case CV_RANGE_10V:         return "0-10V";
    case CV_RANGE_BIPOLAR_5V:  return "+/-5V";
    case CV_RANGE_5V:          return "0-5V";
    case CV_RANGE_3V3:         return "0-3.3V";
    default:                   return "Unknown";
  }
}

static bool cdc_connection_usb(void) {
  return tud_mounted() && tud_cdc_n_connected(0);
}

static bool cdc_connection_cv(void) {
  if (!input_get_cable_detection_enabled()) return true;
  return cv_is_cable_connected();
}

static bool cdc_connection_expression(void) {
  if (!input_get_cable_detection_enabled()) return true;
  return expression_is_connected();
}

static bool cdc_connection_midi_in(void) {
  return midi_in_uart_is_cable_connected();
}

static void cdc_add_connections_json(cJSON *root) {
  if (!root) return;
  cJSON *conn = cJSON_CreateObject();
  if (!conn) return;
  cJSON_AddBoolToObject(conn, "usb", cdc_connection_usb());
  cJSON_AddBoolToObject(conn, "cv", cdc_connection_cv());
  cJSON_AddBoolToObject(conn, "expression", cdc_connection_expression());
  cJSON_AddBoolToObject(conn, "midi_in", cdc_connection_midi_in());
  cJSON_AddItemToObject(root, "connections", conn);
  cJSON_AddStringToObject(root, "cv_range", cdc_cv_range_str(cv_get_range()));
}

static uint8_t s_last_conn_usb = 0xFF;
static uint8_t s_last_conn_cv = 0xFF;
static uint8_t s_last_conn_exp = 0xFF;
static uint8_t s_last_conn_midi_in = 0xFF;

void usb_cdc_notify_connections(void) {
  cdc_push_connections_evt();
}

static void cdc_push_connections_evt(void) {
  if (!s_initialized) return;
  uint8_t usb = cdc_connection_usb() ? 1u : 0u;
  uint8_t cv = cdc_connection_cv() ? 1u : 0u;
  uint8_t exp = cdc_connection_expression() ? 1u : 0u;
  uint8_t midi_in = cdc_connection_midi_in() ? 1u : 0u;
  if (usb == s_last_conn_usb && cv == s_last_conn_cv && exp == s_last_conn_exp &&
      midi_in == s_last_conn_midi_in) {
    return;
  }
  s_last_conn_usb = usb;
  s_last_conn_cv = cv;
  s_last_conn_exp = exp;
  s_last_conn_midi_in = midi_in;
  char buf[40];
  snprintf(buf, sizeof(buf), "EVT:connections:%u:%u:%u:%u",
    (unsigned)usb, (unsigned)cv, (unsigned)exp, (unsigned)midi_in);
  CDC_LOGI("CDC notify: %s", buf);
  cdc_send_notify_line(buf);
}

static void cdc_connections_handler(const event_t *event, void *context) {
  (void)event;
  (void)context;
  cdc_push_connections_evt();
}

void tud_mount_cb(void) {
  cdc_push_connections_evt();
}

void tud_umount_cb(void) {
  cdc_push_connections_evt();
}

static void cdc_send_mem_json(void) {
  task_monitor_heap_snapshot_t snap;
  task_monitor_fill_heap_snapshot(&snap);

  char json[512];
  int n = task_monitor_format_heap_json(&snap, json, sizeof(json));
  if (n <= 0 || (size_t)n >= sizeof(json)) {
    send_response("ERROR: Failed to format memory report");
    return;
  }
  send_json_response(json);
}

static void cdc_send_info_json(void) {
  const device_config_t *cfg = device_config_get();
  const char *slug = cfg ? cfg->pedal_slug : "user.default@0";
  const manifest_device_t *mdev = assets_get_manifest_device(slug);
  const char *assets_csum = version_get_assets_checksum();

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    send_response("ERROR: Out of memory");
    return;
  }

  char ver[16];
  snprintf(ver, sizeof(ver), "%u.%u",
    (unsigned)version_get_major(), (unsigned)version_get_minor());

  cJSON_AddStringToObject(root, "version", ver);
  cJSON_AddNumberToObject(root, "build", version_get_build());
  cJSON_AddStringToObject(root, "git", version_get_git_hash());
  cJSON_AddStringToObject(root, "serial", version_get_serial());
  cJSON_AddStringToObject(root, "assets_checksum", assets_csum ? assets_csum : "");
  cJSON_AddBoolToObject(root, "programming",
    ui_is_in_programming_mode() || screensaver_preserves_programming_session());

  cJSON *pedal = cJSON_CreateObject();
  if (pedal) {
    cJSON_AddStringToObject(pedal, "slug", slug);
    cJSON_AddStringToObject(pedal, "name", mdev ? mdev->name : "Unknown");
    cJSON_AddStringToObject(pedal, "vendor", mdev ? mdev->vendor : "Unknown");
    cJSON_AddNumberToObject(pedal, "midi_channel", cfg ? (unsigned)cfg->midi_channel : 1);
    cJSON_AddBoolToObject(pedal, "send_clock", cfg && cfg->send_clock);
    cJSON_AddStringToObject(pedal, "trs_type",
      cfg ? cdc_trs_type_str(cfg->trs_type) : "unknown");
    cJSON_AddBoolToObject(pedal, "receives_pc", mdev && mdev->receives_pc);
    cJSON_AddBoolToObject(pedal, "transmits_pc", mdev && mdev->transmits_pc);
    cJSON_AddBoolToObject(pedal, "receives_clock", mdev && mdev->receives_clock);
    cJSON_AddBoolToObject(pedal, "receives_notes", mdev && mdev->receives_notes);
    cJSON_AddNumberToObject(pedal, "preset_count", cfg ? (unsigned)cfg->preset_count : 128);
    cJSON_AddStringToObject(pedal, "bank_mode",
      cfg ? cdc_bank_mode_str(cfg->bank_select_mode) : "none");
    cJSON_AddNumberToObject(pedal, "preset_base", cfg ? (unsigned)cfg->preset_base : 0);
    cJSON_AddNumberToObject(pedal, "cc_count", mdev ? (unsigned)mdev->cc_count : 0);
    cJSON_AddItemToObject(root, "pedal", pedal);
  }

  scene_t *scene = scene_get_current();
  if (scene) {
    uint8_t idx = scene_get_current_index();
    uint16_t ordinal = 0;
    uint16_t active_total = 0;
    scene_get_active_slot(idx, &ordinal, &active_total);

    cJSON *scene_obj = cJSON_CreateObject();
    if (scene_obj) {
      cJSON_AddStringToObject(scene_obj, "name", scene->name);
      cJSON_AddStringToObject(scene_obj, "mode", cdc_scene_mode_str(scene_get_mode()));
      cJSON_AddNumberToObject(scene_obj, "active_ordinal", (unsigned)ordinal);
      cJSON_AddNumberToObject(scene_obj, "active_count", (unsigned)active_total);
      cJSON_AddItemToObject(root, "scene", scene_obj);
    }
  }

  cdc_add_connections_json(root);
  cdc_add_clock_json(root);
  cdc_read_clock_snapshot(&s_last_clock_notify);
  s_last_conn_usb = cdc_connection_usb() ? 1u : 0u;
  s_last_conn_cv = cdc_connection_cv() ? 1u : 0u;
  s_last_conn_exp = cdc_connection_expression() ? 1u : 0u;
  s_last_conn_midi_in = cdc_connection_midi_in() ? 1u : 0u;

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (json) {
    send_json_response(json);
    cJSON_free(json);
  } else {
    send_response("ERROR: Out of memory");
  }
}

// INFO is read-only and safe in any CDC mode (web client polls it often).
static bool cdc_try_info_command(const char *cmd) {
  if (strcmp(cmd, "INFO") == 0) {
    cdc_send_info_json();
    return true;
  }
  return false;
}

static uint8_t cdc_resolve_scene_index(const char *arg, bool *is_position) {
  if (!arg || !arg[0]) return 0;
  if (strcmp(arg, "current") == 0) {
    if (is_position) *is_position = false;
    return scene_get_current_index();
  }
  unsigned long pos = strtoul(arg, NULL, 10);
  if (is_position) *is_position = true;
  return scene_get_index_by_position((uint16_t)pos);
}

static void cdc_send_scene_get(const char *arg) {
  const char *idx_arg = arg;
  while (idx_arg && *idx_arg == ' ') idx_arg++;
  if (!idx_arg || !idx_arg[0]) {
    send_response("ERROR: Usage SCENE_GET <position|current>");
    return;
  }

  bool is_position = false;
  uint8_t scene_index = cdc_resolve_scene_index(idx_arg, &is_position);
  if (is_position) {
    unsigned long pos = strtoul(idx_arg, NULL, 10);
    if (pos >= scene_get_total_count()) {
      send_response("ERROR: Invalid scene position");
      return;
    }
  }

  if (!scene_index_in_manifest(scene_index)) {
    send_response("ERROR: Scene not found");
    return;
  }

  char *json = NULL;
  esp_err_t err = scene_get_json(scene_index, &json);
  if (err != ESP_OK || !json) {
    send_response("ERROR: Failed to read scene");
    return;
  }

  size_t len = strlen(json);
  char resp[48];
  snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)len);
  send_response(resp);
  send_binary((const uint8_t *)json, len);
  if (len > 0 && (len % 64) == 0) {
    uint8_t term = '\n';
    send_binary(&term, 1);
  }
  free(json);
  CDC_LOGI("SCENE_GET sent %u bytes for scene %u", (unsigned)len,
    (unsigned)scene_index);
}

static void cdc_cmd_scene_put(const char *args) {
  if (!args) {
    send_response("ERROR: Usage SCENE_PUT <position> <size>");
    return;
  }

  unsigned long position = 0;
  unsigned long size = 0;
  if (sscanf(args, "%lu %lu", &position, &size) != 2) {
    send_response("ERROR: Usage SCENE_PUT <position> <size>");
    return;
  }

  if (position >= scene_get_total_count()) {
    send_response("ERROR: Invalid scene position");
    return;
  }

  if (size == 0 || size > SCENE_JSON_MAX_BYTES) {
    send_response("ERROR: Invalid scene size");
    return;
  }

  if (s_scene_put_buffer) {
    heap_caps_free(s_scene_put_buffer);
    s_scene_put_buffer = NULL;
  }

  s_scene_put_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!s_scene_put_buffer) {
    send_response("ERROR: Out of memory");
    return;
  }

  s_scene_put_index = scene_get_index_by_position((uint16_t)position);
  s_scene_put_size = (size_t)size;
  s_scene_put_received = 0;
  s_state = CDC_STATE_SCENE_RECEIVING;
  CDC_LOGI("SCENE_PUT position %lu index %u size %lu",
    position, (unsigned)s_scene_put_index, size);
  send_response("READY");
}

static void handle_scene_put_binary(const uint8_t *data, size_t len) {
  if (!s_scene_put_buffer || s_state != CDC_STATE_SCENE_RECEIVING) return;

  size_t remaining = s_scene_put_size - s_scene_put_received;
  size_t to_copy = (len < remaining) ? len : remaining;
  memcpy(s_scene_put_buffer + s_scene_put_received, data, to_copy);
  s_scene_put_received += to_copy;

  if (s_scene_put_received < s_scene_put_size) return;

  esp_err_t err = scene_put_json(s_scene_put_index,
    (const char *)s_scene_put_buffer, s_scene_put_size);
  heap_caps_free(s_scene_put_buffer);
  s_scene_put_buffer = NULL;

  if (err == ESP_OK) {
    send_response("OK");
  } else if (err == ESP_ERR_INVALID_ARG) {
    send_response("ERROR: Name already exists or is invalid");
  } else {
    char resp[64];
    snprintf(resp, sizeof(resp), "ERROR: Scene put failed (%s)",
      esp_err_to_name(err));
    send_response(resp);
  }
  s_state = CDC_STATE_IDLE;
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
    (void)cdc_tx_write((const uint8_t *)str, strlen(str), 100, 5);
  }
}

static void console_send_prompt(void) {
  printf("\n> ");
  fflush(stdout);
}

static void process_command(const char *cmd) {
  CDC_LOGI("Received command: '%s' (len=%d)", cmd, strlen(cmd));

  char hex[128] = {0};
  for (int i = 0; i < strlen(cmd) && i < 16; i++) {
    snprintf(hex + strlen(hex), sizeof(hex) - strlen(hex), "%02X ", (uint8_t)cmd[i]);
  }
  CDC_LOGI("Hex: %s", hex);

  if (strncmp(cmd, "FIRMWARE ", 9) == 0) {
    // Parse size
    size_t size = atoi(cmd + 9);
    
    if (size == 0 || size > 8 * 1024 * 1024) {  // Max 8MB
      send_response("ERROR: Invalid firmware size");
      return;
    }

    CDC_LOGI("Starting firmware update (%u bytes)", (unsigned)size);

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
    s_last_progress_sent = 0;
    s_is_firmware = true;
    s_state = CDC_STATE_RECEIVING_FIRMWARE;
    send_response("READY");

    // Publish update started event for UI
    event_t update_event = {
      .type = EVENT_UPDATE_STARTED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.update = {
        .update_type = UPDATE_TYPE_FIRMWARE,
        .total_size = size
      }
    };
    event_bus_post(&update_event);

  } else if (strncmp(cmd, "ASSETS ", 7) == 0) {
    // Parse size and optional checksum: "ASSETS <size> [checksum]"
    size_t size = 0;
    char checksum[9] = {0};
    int parsed = sscanf(cmd + 7, "%zu %8s", &size, checksum);

    if (parsed < 1 || size == 0 || size > 16 * 1024 * 1024) {  // Max 16MB
      send_response("ERROR: Invalid assets size");
      return;
    }

    // Store pending checksum if provided
    if (parsed >= 2 && strlen(checksum) == 8) {
      strncpy(s_pending_assets_checksum, checksum, sizeof(s_pending_assets_checksum) - 1);
      s_pending_assets_checksum[sizeof(s_pending_assets_checksum) - 1] = '\0';
      CDC_LOGI("Starting assets update (%u bytes, checksum %s)",
        (unsigned)size, s_pending_assets_checksum);
    } else {
      s_pending_assets_checksum[0] = '\0';  // No checksum provided
      CDC_LOGI("Starting assets update (%u bytes)", (unsigned)size);
    }

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
    s_last_progress_sent = 0;
    s_is_firmware = false;
    s_state = CDC_STATE_RECEIVING_ASSETS;
    send_response("READY");

    // Publish update started event for UI
    event_t update_event = {
      .type = EVENT_UPDATE_STARTED,
      .priority = EVENT_PRIORITY_HIGH,
      .timestamp = event_bus_get_current_timestamp(),
      .data.update = {
        .update_type = UPDATE_TYPE_ASSETS,
        .total_size = size
      }
    };
    event_bus_post(&update_event);

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
    CDC_LOGI("Committing %s update", s_is_firmware ? "firmware" : "assets");

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
      // Save the assets checksum to NVS after successful update
      if (err == ESP_OK && s_pending_assets_checksum[0] != '\0') {
        CDC_LOGI("Saving assets checksum to NVS: %s", s_pending_assets_checksum);
        esp_err_t csum_err = version_set_assets_checksum(s_pending_assets_checksum);
        if (csum_err != ESP_OK) {
          ESP_LOGE(TAG, "Failed to save assets checksum: %s", esp_err_to_name(csum_err));
        }
        s_pending_assets_checksum[0] = '\0';
      } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "Assets update succeeded but no checksum was provided");
      }
    }

    if (err == ESP_OK) {
      CDC_LOGI("Update successful");
      send_response("SUCCESS");
      s_state = CDC_STATE_IDLE;

      // Publish update complete event (success)
      event_t complete_event = {
        .type = EVENT_UPDATE_COMPLETE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.update = {
          .update_type = s_is_firmware ? UPDATE_TYPE_FIRMWARE : UPDATE_TYPE_ASSETS,
          .success = 1
        }
      };
      event_bus_post(&complete_event);
    } else {
      ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
      send_response("ERROR: Update failed");
      s_state = CDC_STATE_ERROR;

      // Publish update complete event (failure)
      event_t complete_event = {
        .type = EVENT_UPDATE_COMPLETE,
        .priority = EVENT_PRIORITY_HIGH,
        .timestamp = event_bus_get_current_timestamp(),
        .data.update = {
          .update_type = s_is_firmware ? UPDATE_TYPE_FIRMWARE : UPDATE_TYPE_ASSETS,
          .success = 0
        }
      };
      event_bus_post(&complete_event);
    }

    // Free buffer
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
      s_update_buffer = NULL;
    }
    s_update_size = 0;
    s_received_bytes = 0;

  } else if (strcmp(cmd, "RESET") == 0) {
    CDC_LOGI("Reset command received. Rebooting...");
    send_response("RESETTING");
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time to send response
    esp_restart();

  } else if (strcmp(cmd, "STATUS") == 0) {
    uint8_t progress = usb_cdc_update_get_progress();
    char resp[32];
    snprintf(resp, sizeof(resp), "PROGRESS %u", progress);
    send_response(resp);

  } else if (strcmp(cmd, "CANCEL") == 0) {
    CDC_LOGI("Update cancelled");
    if (s_update_buffer) {
      heap_caps_free(s_update_buffer);
      s_update_buffer = NULL;
    }
    s_state = CDC_STATE_IDLE;
    s_update_size = 0;
    s_received_bytes = 0;
    send_response("CANCELLED");

  } else if (strcmp(cmd, "CONSOLE") == 0) {
    CDC_LOGI("Entering console mode");
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
    CDC_LOGI("Entering assets management mode");
    s_state = CDC_STATE_ASSETS;
    s_assets_path[0] = '\0';
    s_assets_file = NULL;
    s_assets_file_size = 0;
    s_assets_bytes_transferred = 0;
    send_response("ASSETS_STARTED");

  } else if (strcmp(cmd, "DISPLAY") == 0) {
    CDC_LOGI("Entering display streaming mode");
    
    // If already streaming (e.g. previous client disconnected uncleanly), stop first
    if (lvgl_stream_is_active()) {
      CDC_LOGI("Stopping previous stream session");
      lvgl_stream_stop();
      vTaskDelay(pdMS_TO_TICKS(50));  // Let TX task clean up
    }
    
    // Initialize stream if not already done
    esp_err_t err = lvgl_stream_init();
    if (err != ESP_OK) {
      send_response("ERROR: Failed to initialize display stream");
      return;
    }
    
    // Set dimensions from display driver
    uint16_t w = display_get_width();
    uint16_t h = display_get_height();
    lvgl_stream_set_dimensions(w, h);
    
    // Reset stats for new session
    lvgl_stream_reset_stats();
    
    // Start streaming
    err = lvgl_stream_start();
    if (err != ESP_OK) {
      send_response("ERROR: Failed to start display stream");
      return;
    }
    
    s_state = CDC_STATE_DISPLAY;
    
    // Send dimensions so client knows what to expect
    char resp[64];
    snprintf(resp, sizeof(resp), "DISPLAY_STARTED %u %u", (unsigned)w, (unsigned)h);
    send_response(resp);

  } else if (strcmp(cmd, "DEVICE") == 0) {
    const char* slug = device_config_get_pedal_slug();
    char resp[80];
    snprintf(resp, sizeof(resp), "DEVICE %s", slug ? slug : "user.default@0");
    send_response(resp);

  } else if (strcmp(cmd, "MIDI") == 0) {
    CDC_LOGI("Entering MIDI relay mode");
    s_state = CDC_STATE_MIDI_RELAY;
    s_midi_relay_active = true;
    s_midi_relay_show_clock = false;
    
    // Subscribe to MIDI IN events
    event_bus_subscribe(EVENT_MIDI_IN, midi_relay_event_handler, NULL);
    
    send_response("MIDI_STARTED");

  } else if (strncmp(cmd, "MIDI ", 5) == 0) {
    // MIDI with options, e.g., "MIDI CLOCK"
    CDC_LOGI("Entering MIDI relay mode with options");
    s_state = CDC_STATE_MIDI_RELAY;
    s_midi_relay_active = true;
    s_midi_relay_show_clock = (strstr(cmd + 5, "CLOCK") != NULL);
    
    event_bus_subscribe(EVENT_MIDI_IN, midi_relay_event_handler, NULL);
    
    send_response("MIDI_STARTED");

  } else if (strcmp(cmd, "SETTINGS") == 0) {
    CDC_LOGI("Entering settings mode");
    s_state = CDC_STATE_SETTINGS;
    send_response("SETTINGS_STARTED");

  } else if (strcmp(cmd, "CONFIG") == 0) {
    CDC_LOGI("Entering config mode");
    s_state = CDC_STATE_CONFIG;
    send_response("CONFIG_STARTED");

  } else if (strcmp(cmd, "SCENES") == 0) {
    CDC_LOGI("Entering scenes mode");
    s_state = CDC_STATE_SCENES;
    send_response("SCENES_STARTED");

  } else if (strcmp(cmd, "PEDALS") == 0) {
    CDC_LOGI("Entering pedals mode");
    s_state = CDC_STATE_PEDALS;
    send_response("PEDALS_STARTED");

  } else if (strcmp(cmd, "INFO") == 0) {
    cdc_send_info_json();

  } else if (strcmp(cmd, "MEM") == 0) {
    cdc_send_mem_json();

  } else if (strncmp(cmd, "MEM ", 4) == 0) {
    const char *sub = cmd + 4;
    while (*sub == ' ') sub++;
    if (strcmp(sub, "TRACE") == 0) {
      task_monitor_heap_trace_dump();
      send_response("MEM_TRACE_DONE");
    } else if (strcmp(sub, "TRACE START") == 0) {
      esp_err_t err = task_monitor_heap_trace_start();
      send_response(err == ESP_OK ? "MEM_TRACE_STARTED" : "ERROR: trace start failed");
    } else if (strcmp(sub, "TRACE STOP") == 0) {
      esp_err_t err = task_monitor_heap_trace_stop();
      send_response(err == ESP_OK ? "MEM_TRACE_STOPPED" : "ERROR: trace stop failed");
    } else {
      send_response("ERROR: Unknown MEM subcommand");
    }

  } else if (strncmp(cmd, "NAV ", 4) == 0) {
    const char *op = cmd + 4;
    while (op && *op == ' ') op++;
    scene_mode_t mode = scene_get_mode();
    if (mode == SCENE_MODE_SINGLE) {
      send_response("ERROR: Navigation not available in Single mode");
    } else if (strcmp(op, "PREV") == 0) {
      esp_err_t err = cdc_scene_apply_if_pending(scene_previous());
      send_response(err == ESP_OK ? "OK" : "ERROR: Scene previous failed");
    } else if (strcmp(op, "NEXT") == 0) {
      esp_err_t err = cdc_scene_apply_if_pending(scene_next());
      send_response(err == ESP_OK ? "OK" : "ERROR: Scene next failed");
    } else {
      send_response("ERROR: Unknown navigation command");
    }

  } else if (strcmp(cmd, "SCENE CONFIRM") == 0) {
    esp_err_t err = scene_confirm_change();
    send_response(err == ESP_OK ? "OK" : "ERROR: Scene confirm failed");

  } else if (strcmp(cmd, "SCENE CANCEL") == 0) {
    esp_err_t err = scene_cancel_pending();
    send_response(err == ESP_OK ? "OK" : "ERROR: Scene cancel failed");

  } else if (cdc_try_pedal_set_command(cmd)) {
    /* handled */

  } else if (strncmp(cmd, "TRANSPORT ", 10) == 0) {
    const char *op = cmd + 10;
    while (op && *op == ' ') op++;
    if (strcmp(op, "PLAY") == 0) {
      transport_play();
      send_response("OK");
    } else if (strcmp(op, "STOP") == 0) {
      transport_stop();
      send_response("OK");
    } else if (strcmp(op, "RECORD") == 0) {
      transport_record();
      send_response("OK");
    } else {
      send_response("ERROR: Unknown transport command");
    }

  } else if (strncmp(cmd, "SCENE_INSPECT", 13) == 0) {
    const char *arg = cmd + 13;
    while (arg && *arg == ' ') arg++;
    cdc_send_scene_inspect(arg && *arg ? arg : NULL);

  } else if (strncmp(cmd, "SCENE_GET ", 10) == 0) {
    cdc_send_scene_get(cmd + 10);

  } else if (strncmp(cmd, "SCENE_PUT ", 10) == 0) {
    cdc_cmd_scene_put(cmd + 10);

  } else if (strcmp(cmd, "EXIT") == 0) {
    // EXIT in idle state is a no-op (already idle)
    // Don't send any response - this prevents spurious errors after successful updates
    CDC_LOGD("EXIT received in idle state, ignoring");

  } else {
    ESP_LOGW(TAG, "Unknown command: %s", cmd);
    send_response("ERROR: Unknown command");
  }
}

static void handle_binary_data(const uint8_t *data, size_t len) {
  // Debug first chunk
  if (s_received_bytes == 0) {
    CDC_LOGI("Received first chunk of data (%u bytes)", (unsigned)len);
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
  uint8_t progress = usb_cdc_update_get_progress();
  if (progress >= s_last_progress_sent + 10) {
    char resp[32];
    snprintf(resp, sizeof(resp), "PROGRESS %u", progress);
    send_response(resp);
    s_last_progress_sent = progress;
  }

  if (s_received_bytes >= s_update_size) {
    CDC_LOGI("Transfer complete (%u bytes)", (unsigned)s_received_bytes);
    send_response("TRANSFER_COMPLETE");
    s_state = CDC_STATE_WAITING_COMMIT;

    // Publish progress event indicating flash phase is starting
    event_t progress_event = {
      .type = EVENT_UPDATE_PROGRESS,
      .priority = EVENT_PRIORITY_NORMAL,
      .timestamp = event_bus_get_current_timestamp(),
      .data.update = {
        .update_type = s_is_firmware ? UPDATE_TYPE_FIRMWARE : UPDATE_TYPE_ASSETS,
        .phase = UPDATE_PHASE_FLASH,
        .percent = 100
      }
    };
    event_bus_post(&progress_event);
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
    
    CDC_LOGI("Exiting console mode");
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

// Compute the absolute filesystem path for a CDC argument.
// Phase 4 protocol:
//   - Absolute paths under /assets or /userdata are taken verbatim.
//   - Other absolute paths are rejected by setting dest to "" (empty); callers
//     that don't pre-validate via is_writable_path will see fopen/stat fail.
//   - Relative paths default to USERDATA_BASE_PATH (the writable mount). This
//     keeps older Ruby tooling that passes bare names working without changes.
static void build_full_path(char *dest, size_t dest_size, const char *path) {
  if (!path || dest_size == 0) {
    if (dest && dest_size) dest[0] = '\0';
    return;
  }
  if (path[0] == '/') {
    if (strncmp(path, ASSETS_BASE_PATH "/", strlen(ASSETS_BASE_PATH) + 1) == 0
        || strcmp(path, ASSETS_BASE_PATH) == 0
        || strncmp(path, USERDATA_BASE_PATH "/", strlen(USERDATA_BASE_PATH) + 1) == 0
        || strcmp(path, USERDATA_BASE_PATH) == 0) {
      strncpy(dest, path, dest_size - 1);
      dest[dest_size - 1] = '\0';
      return;
    }
    // Absolute path outside both managed mounts: pass through unchanged so
    // callers see consistent error behavior (stat/fopen will fail with ENOENT
    // or EACCES). Mutation gates also reject these.
    strncpy(dest, path, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return;
  }
  snprintf(dest, dest_size, "%s/%s", USERDATA_BASE_PATH, path);
  dest[dest_size - 1] = '\0';
}

// True iff path lies on the writable userdata partition. Used as the gate on
// every CDC mutation (PUT/MKDIR/RM/RMRF/MV/CP/EXTRACT) so the host cannot
// scribble over the read-only shared content.
static bool is_writable_path(const char *full_path) {
  if (!full_path) return false;
  size_t plen = strlen(USERDATA_BASE_PATH);
  if (strncmp(full_path, USERDATA_BASE_PATH, plen) != 0) return false;
  // Must be exactly /userdata or start with /userdata/.
  return full_path[plen] == '\0' || full_path[plen] == '/';
}

// manifest.json under scenes/ or devices/ is the index file — never mutate via ASSETS.
static bool assets_is_protected_system_file(const char *full_path) {
  if (!full_path) return false;
  const char *leaf = strrchr(full_path, '/');
  leaf = leaf ? leaf + 1 : full_path;
  if (strcasecmp(leaf, "manifest.json") != 0) return false;
  static const char *prefixes[] = {
    USERDATA_BASE_PATH "/scenes",
    USERDATA_BASE_PATH "/devices",
  };
  for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
    size_t plen = strlen(prefixes[i]);
    if (strncmp(full_path, prefixes[i], plen) == 0 &&
        (full_path[plen] == '\0' || full_path[plen] == '/')) {
      return true;
    }
  }
  return false;
}

static void reject_protected_system_file(const char *full_path) {
  char resp[256];
  snprintf(resp, sizeof(resp),
    "ERROR: protected system file: %s", full_path);
  send_response(resp);
}

// Send a uniform "read-only" rejection. Centralized so the wording matches
// the web app's expectations (web/js/assets.js looks for the prefix).
static void reject_readonly(const char *full_path) {
  char resp[256];
  snprintf(resp, sizeof(resp),
    "ERROR: read-only path: %s (use /userdata/... for writes)", full_path);
  send_response(resp);
}

// Process assets mode commands
static void process_assets_command(const char *cmd) {
  CDC_LOGD("Assets cmd: %s", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave assets mode
  if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "exit") == 0) {
    CDC_LOGI("Exiting assets mode");
    s_state = CDC_STATE_IDLE;
    send_response("ASSETS_STOPPED");
    return;
  }

  // ASSETS while already in assets mode (idempotent re-handshake)
  if (strcmp(cmd, "ASSETS") == 0) {
    send_response("ASSETS_STARTED");
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
  
  // RMRF <path> - recursive delete
  if (strncmp(cmd, "RMRF ", 5) == 0) {
    assets_cmd_rmrf(cmd + 5);
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
  
  // EXTRACT <path> <size> - upload and extract ZIP
  if (strncmp(cmd, "EXTRACT ", 8) == 0) {
    const char *args = cmd + 8;
    char path[MAX_PATH_LEN];
    size_t size = 0;
    if (sscanf(args, "%255s %zu", path, &size) == 2) {
      assets_cmd_extract(path, size);
    } else {
      send_response("ERROR: Usage: EXTRACT <dest_path> <size>");
    }
    return;
  }
  
  // Unknown command
  send_response("ERROR: Unknown assets command");
}

// LS - list directory.
// Phase 4: a literal `/` enumerates the two top-level mount points (so the
// web file browser can render a two-root view) instead of trying to opendir
// a non-existent root vfs. Any other path is dispatched normally.
static void assets_cmd_ls(const char *path) {
  if (path && (strcmp(path, "/") == 0 || strcmp(path, "") == 0)) {
    char response[256];
    snprintf(response, sizeof(response),
      "[{\"name\":\"assets\",\"type\":\"dir\",\"size\":0,\"readonly\":true},"
      "{\"name\":\"userdata\",\"type\":\"dir\",\"size\":0,\"readonly\":false,\"available\":%s}]",
      assets_userdata_available() ? "true" : "false");
    send_response(response);
    return;
  }

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

// DF - filesystem stats for both partitions. The web app reads this to draw
// two usage bars (one per partition). The legacy single-object response is
// gone; clients expect the new shape:
//   {"assets":{"total":N,"used":N,"free":N},
//    "userdata":{"total":N,"used":N,"free":N,"available":bool}}
// userdata.available is false when the partition is missing entirely (the
// degraded-boot path); web/js/assets.js shows a banner in that case.
static void assets_cmd_df(void) {
  size_t a_total = 0, a_used = 0;
  size_t u_total = 0, u_used = 0;

  esp_err_t a_err = esp_littlefs_info(ASSETS_PARTITION, &a_total, &a_used);
  bool userdata_ok = assets_userdata_available();
  esp_err_t u_err = userdata_ok
    ? esp_littlefs_info(USERDATA_PARTITION, &u_total, &u_used)
    : ESP_ERR_NOT_FOUND;

  if (a_err != ESP_OK) {
    send_response("ERROR: Cannot get filesystem info");
    return;
  }

  char response[256];
  snprintf(response, sizeof(response),
    "{\"assets\":{\"total\":%u,\"used\":%u,\"free\":%u},"
    "\"userdata\":{\"total\":%u,\"used\":%u,\"free\":%u,\"available\":%s}}",
    (unsigned)a_total, (unsigned)a_used, (unsigned)(a_total - a_used),
    (unsigned)u_total, (unsigned)u_used,
    (unsigned)(u_total > u_used ? u_total - u_used : 0),
    (userdata_ok && u_err == ESP_OK) ? "true" : "false");
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

  if (!is_writable_path(full_path)) { reject_readonly(full_path); return; }

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

  if (!is_writable_path(full_path)) { reject_readonly(full_path); return; }
  if (assets_is_protected_system_file(full_path)) {
    reject_protected_system_file(full_path);
    return;
  }

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

// RMRF - recursive delete
static void assets_cmd_rmrf(const char *path) {
  char full_path[MAX_PATH_LEN];
  build_full_path(full_path, sizeof(full_path), path);

  if (!is_writable_path(full_path)) { reject_readonly(full_path); return; }

  struct stat st;
  if (stat(full_path, &st) != 0) {
    send_response("ERROR: Path not found");
    return;
  }

  esp_err_t result = assets_recursive_delete(full_path);

  if (result == ESP_OK) {
    // Trigger manifest update for the folder
    assets_file_deleted(full_path);
    send_response("OK");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Recursive delete failed: %s", esp_err_to_name(result));
    send_response(resp);
  }
}

// MV - move/rename. Both src and dst must be writable; preventing /assets ->
// /userdata copies via MV keeps the protocol simple. Use CP if you really
// want to seed user content from the shared partition.
static void assets_cmd_mv(const char *src, const char *dst) {
  char full_src[MAX_PATH_LEN], full_dst[MAX_PATH_LEN];
  build_full_path(full_src, sizeof(full_src), src);
  build_full_path(full_dst, sizeof(full_dst), dst);

  if (!is_writable_path(full_src)) { reject_readonly(full_src); return; }
  if (!is_writable_path(full_dst)) { reject_readonly(full_dst); return; }
  if (assets_is_protected_system_file(full_src) ||
      assets_is_protected_system_file(full_dst)) {
    reject_protected_system_file(
      assets_is_protected_system_file(full_dst) ? full_dst : full_src);
    return;
  }

  if (rename(full_src, full_dst) == 0) {
    assets_file_deleted(full_src);
    assets_file_created(full_dst);
    send_response("OK");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot move: %s", strerror(errno));
    send_response(resp);
  }
}

// CP - copy file. Source may be RO (legitimate seed-from-shared use case);
// only the destination must be writable.
static void assets_cmd_cp(const char *src, const char *dst) {
  char full_src[MAX_PATH_LEN], full_dst[MAX_PATH_LEN];
  build_full_path(full_src, sizeof(full_src), src);
  build_full_path(full_dst, sizeof(full_dst), dst);

  if (!is_writable_path(full_dst)) { reject_readonly(full_dst); return; }

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

  if (!is_writable_path(s_assets_path)) { reject_readonly(s_assets_path); return; }
  if (assets_is_protected_system_file(s_assets_path)) {
    reject_protected_system_file(s_assets_path);
    return;
  }

  if (assets_validate_user_pedal_put(s_assets_path) != ESP_OK) {
    send_response("ERROR: A pedal with this slug already exists");
    return;
  }

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
  
  CDC_LOGI("Receiving file: %s (%u bytes)", s_assets_path, (unsigned)size);
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
  CDC_LOGI("Sending file: %s (%u bytes)", full_path, (unsigned)st.st_size);
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
    // When the payload length is an exact multiple of the 64-byte full-speed
    // bulk packet size, the USB host withholds the final full packet until it
    // sees a short packet. Emit a one-byte terminator so the host flushes the
    // payload immediately; the JS side discards this trailing byte.
    if (s_assets_file_size > 0 && (s_assets_file_size % 64) == 0) {
      uint8_t term = '\n';
      send_binary(&term, 1);
    }
    CDC_LOGI("File send complete: %u bytes", (unsigned)s_assets_bytes_transferred);
  }
}

// Handle binary data for file upload
static void handle_assets_binary_data(const uint8_t *data, size_t len) {
  size_t remaining = s_assets_file_size - s_assets_bytes_transferred;
  size_t to_copy = (len > remaining) ? remaining : len;
  
  if (s_extract_mode) {
    // EXTRACT mode: copy to PSRAM buffer
    if (!s_extract_buffer) {
      ESP_LOGE(TAG, "No extract buffer allocated");
      s_state = CDC_STATE_ASSETS;
      s_extract_mode = false;
      send_response("ERROR: No buffer");
      return;
    }
    
    memcpy(s_extract_buffer + s_assets_bytes_transferred, data, to_copy);
    s_assets_bytes_transferred += to_copy;
    
    // Check if done
    if (s_assets_bytes_transferred >= s_assets_file_size) {
      CDC_LOGI("ZIP receive complete: %u bytes", (unsigned)s_assets_bytes_transferred);
      
      // Store size for the extract task
      s_extract_size = s_assets_file_size;
      s_extract_mode = false;
      s_extract_in_progress = true;
      s_state = CDC_STATE_ASSETS;
      
      // Spawn extract task with large stack (miniz needs significant stack)
      BaseType_t ret = xTaskCreate(
        extract_task,       // Task function
        "extract_task",     // Task name
        16384,              // Stack size (16KB internal RAM)
        NULL,               // Task parameter
        4,                  // Priority
        &s_extract_task_handle
      );
      
      if (ret != pdPASS) {
        // Failed to create task - cleanup
        heap_caps_free(s_extract_buffer);
        s_extract_buffer = NULL;
        s_extract_in_progress = false;
        ESP_LOGE(TAG, "Failed to create extract task");
        send_response("ERROR: Failed to create extract task");
      }
      // Response will be sent by extract_task
    }
  } else {
    // Normal PUT mode: write to file
    if (!s_assets_file) {
      ESP_LOGE(TAG, "No file open for writing");
      s_state = CDC_STATE_ASSETS;
      send_response("ERROR: No file open");
      return;
    }
    
    size_t written = fwrite(data, 1, to_copy, s_assets_file);
    s_assets_bytes_transferred += written;
    
    if (written != to_copy) {
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
      
      CDC_LOGI("File receive complete: %s (%u bytes)", 
        s_assets_path, (unsigned)s_assets_bytes_transferred);

      if (strstr(s_assets_path, "/devices/user/") != NULL &&
          strstr(s_assets_path, ".json") != NULL) {
        if (assets_validate_device_json_file(s_assets_path) != ESP_OK) {
          unlink(s_assets_path);
          s_state = CDC_STATE_ASSETS;
          send_response("ERROR: Duplicate controlChangeNumber");
          return;
        }
      }
      
      // Trigger manifest update
      assets_file_created(s_assets_path);
      
      s_state = CDC_STATE_ASSETS;
      send_response("OK");
    }
  }
}

// MANIFEST <type> - download a manifest JSON file (SIZE + binary, same as GET).
// Phase 4: types are partition-aware:
//   shared_devices -> /assets/devices/manifest.json   (RO, can be ~100KB)
//   user_devices   -> /userdata/devices/manifest.json (RW)
//   scenes         -> /userdata/scenes/manifest.json  (RW)
//   images         -> /assets/images/manifest.json    (RO)
// Legacy `devices` resolves to shared_devices.
// Response: "SIZE <bytes>" then raw file bytes (CDC_STATE_ASSETS_SENDING).
static void assets_cmd_manifest(const char *type) {
  char manifest_path[MAX_PATH_LEN];

  if (strcmp(type, "scenes") == 0) {
    snprintf(manifest_path, sizeof(manifest_path),
      "%s/scenes/manifest.json", USERDATA_BASE_PATH);
  } else if (strcmp(type, "shared_devices") == 0 || strcmp(type, "devices") == 0) {
    snprintf(manifest_path, sizeof(manifest_path),
      "%s/devices/manifest.json", ASSETS_BASE_PATH);
  } else if (strcmp(type, "user_devices") == 0) {
    snprintf(manifest_path, sizeof(manifest_path),
      "%s/devices/manifest.json", USERDATA_BASE_PATH);
  } else if (strcmp(type, "images") == 0) {
    snprintf(manifest_path, sizeof(manifest_path),
      "%s/images/manifest.json", ASSETS_BASE_PATH);
  } else {
    send_response("ERROR: Unknown manifest type "
      "(use: scenes, shared_devices, user_devices, images)");
    return;
  }

  struct stat st;
  if (stat(manifest_path, &st) != 0) {
    send_response("ERROR: Manifest not found");
    return;
  }

  if (S_ISDIR(st.st_mode)) {
    send_response("ERROR: Manifest path is a directory");
    return;
  }

  if (st.st_size > 512 * 1024) {
    send_response("ERROR: Manifest too large");
    return;
  }

  if (s_assets_file) {
    fclose(s_assets_file);
    s_assets_file = NULL;
  }

  s_assets_file = fopen(manifest_path, "rb");
  if (!s_assets_file) {
    send_response("ERROR: Cannot open manifest");
    return;
  }

  strncpy(s_assets_path, manifest_path, sizeof(s_assets_path) - 1);
  s_assets_path[sizeof(s_assets_path) - 1] = '\0';
  s_assets_file_size = st.st_size;
  s_assets_bytes_transferred = 0;

  char resp[64];
  snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)st.st_size);
  send_response(resp);

  s_state = CDC_STATE_ASSETS_SENDING;
  CDC_LOGI("Sending manifest %s (%u bytes)", manifest_path, (unsigned)st.st_size);
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

      if (assets_zip_skip_archive_path(archive_name)) continue;
      
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

      if (assets_zip_skip_archive_path(archive_name)) continue;
      
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
      
      CDC_LOGD("ZIP: Added %s (%u bytes)", archive_name, (unsigned)read_bytes);
      free(file_buf);
    }
  }
  
  free(full_path);
  free(archive_name);
  closedir(dir);
  return true;
}

// EXTRACT task - runs with large stack, deletes itself when done
static void extract_task(void *arg) {
  (void)arg;  // Unused
  
  CDC_LOGI("Extract task started for %s", s_extract_dest);
  
  // Extract the ZIP from PSRAM buffer
  esp_err_t result = assets_extract_zip(s_extract_buffer, s_extract_size, s_extract_dest);
  
  // Free the buffer
  heap_caps_free(s_extract_buffer);
  s_extract_buffer = NULL;
  
  if (result == ESP_OK) {
    // Trigger manifest updates for the destination folder
    assets_file_created(s_extract_dest);
    send_response("OK");
    CDC_LOGI("Extract completed successfully");
  } else {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Extract failed: %s", esp_err_to_name(result));
    send_response(resp);
    ESP_LOGE(TAG, "Extract failed: %s", esp_err_to_name(result));
  }
  
  s_extract_in_progress = false;
  s_extract_task_handle = NULL;
  vTaskDelete(NULL);  // Delete self
}

// ZIP task - runs with large stack, deletes itself when done
static void zip_task(void *arg) {
  const char *full_path = (const char *)arg;
  
  CDC_LOGI("ZIP task started for %s", full_path);
  
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
  
  CDC_LOGI("ZIP archive created: %u bytes", (unsigned)archive_size);
  
  // Send size response
  char resp[64];
  snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)archive_size);
  send_response(resp);
  
  // Stream the archive data
  send_binary((const uint8_t *)archive_buf, archive_size);
  // Short-packet terminator for exact-multiple-of-64 payloads (see handle_assets_send).
  if (archive_size > 0 && (archive_size % 64) == 0) {
    uint8_t term = '\n';
    send_binary(&term, 1);
  }
  
  // Cleanup miniz (also frees archive_buf since we used heap mode)
  mz_zip_writer_end(&zip);
  
  CDC_LOGI("ZIP archive sent successfully");

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
  
  CDC_LOGI("Spawning ZIP task for %s", s_zip_path);
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

// EXTRACT - receive ZIP and extract to destination
static void assets_cmd_extract(const char *path, size_t size) {
  // Build destination path
  build_full_path(s_extract_dest, sizeof(s_extract_dest), path);

  if (!is_writable_path(s_extract_dest)) {
    reject_readonly(s_extract_dest);
    return;
  }
  if (assets_is_blocked_extract_dest(s_extract_dest)) {
    send_response("ERROR: Cannot extract into cache folder");
    return;
  }

  // Verify destination exists and is a directory
  struct stat st;
  if (stat(s_extract_dest, &st) != 0) {
    // Try to create it
    if (mkdir(s_extract_dest, 0755) != 0) {
      char resp[128];
      snprintf(resp, sizeof(resp), "ERROR: Cannot create destination: %s", path);
      send_response(resp);
      return;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    send_response("ERROR: Destination must be a directory");
    return;
  }
  
  // Allocate PSRAM buffer for ZIP data
  s_extract_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!s_extract_buffer) {
    char resp[128];
    snprintf(resp, sizeof(resp), "ERROR: Cannot allocate %u bytes in PSRAM", (unsigned)size);
    send_response(resp);
    return;
  }
  
  s_assets_file_size = size;
  s_assets_bytes_transferred = 0;
  s_extract_mode = true;
  s_state = CDC_STATE_ASSETS_RECEIVING;
  
  CDC_LOGI("Receiving ZIP for extraction: %u bytes -> %s", (unsigned)size, s_extract_dest);
  send_response("READY");
}

// ============================================================================
// MIDI Relay Mode
// ============================================================================

static void midi_relay_stop(void) {
  if (s_midi_relay_active) {
    event_bus_unsubscribe(EVENT_MIDI_IN, midi_relay_event_handler);
    s_midi_relay_active = false;
    CDC_LOGI("MIDI relay stopped");
  }
}

static void midi_relay_event_handler(const event_t* event, void* context) {
  (void)context;
  
  if (!s_midi_relay_active || s_state != CDC_STATE_MIDI_RELAY) return;
  if (event->type != EVENT_MIDI_IN) return;
  
  uint8_t type = event->data.midi_in.type;
  uint8_t channel = event->data.midi_in.channel;
  uint8_t data1 = event->data.midi_in.data1;
  uint8_t data2 = event->data.midi_in.data2;
  uint16_t length = event->data.midi_in.length;
  
  // Skip clock unless enabled
  if (type == MIDI_EVENT_REALTIME_CLOCK && !s_midi_relay_show_clock) return;
  
  // Skip active sensing (too noisy for relay)
  if (type == MIDI_EVENT_ACTIVE_SENSING) return;
  
  // Format: M:<type>,<channel>,<data1>,<data2>,<length>[,<hex_sysex>]
  char buf[256];
  
  if (type == MIDI_EVENT_SYS_EX && event->data.midi_in.sysex_data && length > 0) {
    // Include hex SysEx data (limited to first 64 bytes)
    int pos = snprintf(buf, sizeof(buf), "M:%d,%d,%d,%d,%d,", 
      type, channel, data1, data2, length);
    
    size_t hex_len = (length > 64) ? 64 : length;
    for (size_t i = 0; i < hex_len && pos < (int)sizeof(buf) - 3; i++) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X", 
        event->data.midi_in.sysex_data[i]);
    }
    if (length > 64) {
      strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
    }
  } else {
    snprintf(buf, sizeof(buf), "M:%d,%d,%d,%d,%d", 
      type, channel, data1, data2, length);
  }
  
  // Send over CDC with newline
  tud_cdc_write(buf, strlen(buf));
  tud_cdc_write("\n", 1);
  tud_cdc_write_flush();
}

// ============================================================================
// Settings Mode - Direct NVS access without console REPL overhead
// ============================================================================

#define NVS_NAMESPACE "app_settings"

// Helper to get type string from NVS type
static const char* nvs_type_to_str(nvs_type_t type) {
  switch (type) {
    case NVS_TYPE_U8: return "u8";
    case NVS_TYPE_I8: return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "str";
    case NVS_TYPE_BLOB: return "blob";
    default: return "unknown";
  }
}

// LIST - enumerate all NVS keys
static void settings_cmd_list(void) {
  nvs_iterator_t it = NULL;
  esp_err_t err = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);
  
  if (err == ESP_ERR_NVS_NOT_FOUND || it == NULL) {
    send_response("END");
    return;
  }
  
  char line[80];
  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    snprintf(line, sizeof(line), "%s:%s", info.key, nvs_type_to_str(info.type));
    send_response(line);
    
    err = nvs_entry_next(&it);
    if (err != ESP_OK) break;
  }
  
  nvs_release_iterator(it);
  send_response("END");
}

// GET <key> - get value for a key
static void settings_cmd_get(const char *key) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    send_response("ERROR: Cannot open NVS");
    return;
  }
  
  // Try to find the key and its type
  nvs_iterator_t it = NULL;
  err = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);
  
  nvs_type_t key_type = NVS_TYPE_ANY;
  bool found = false;
  
  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    if (strcmp(info.key, key) == 0) {
      key_type = info.type;
      found = true;
      break;
    }
    
    err = nvs_entry_next(&it);
    if (err != ESP_OK) break;
  }
  nvs_release_iterator(it);
  
  if (!found) {
    nvs_close(handle);
    send_response("ERROR: Key not found");
    return;
  }
  
  char resp[256];
  
  switch (key_type) {
    case NVS_TYPE_U8: {
      uint8_t val;
      if (nvs_get_u8(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%u", val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_I8: {
      int8_t val;
      if (nvs_get_i8(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%d", val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_U16: {
      uint16_t val;
      if (nvs_get_u16(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%u", val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_I16: {
      int16_t val;
      if (nvs_get_i16(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%d", val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_U32: {
      uint32_t val;
      if (nvs_get_u32(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%u", (unsigned)val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_I32: {
      int32_t val;
      if (nvs_get_i32(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%d", (int)val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_U64: {
      uint64_t val;
      if (nvs_get_u64(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%llu", (unsigned long long)val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_I64: {
      int64_t val;
      if (nvs_get_i64(handle, key, &val) == ESP_OK) {
        snprintf(resp, sizeof(resp), "%lld", (long long)val);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    case NVS_TYPE_STR: {
      size_t len = 0;
      if (nvs_get_str(handle, key, NULL, &len) == ESP_OK && len < 256) {
        char *str = malloc(len);
        if (str && nvs_get_str(handle, key, str, &len) == ESP_OK) {
          send_response(str);
        } else {
          send_response("ERROR: Read failed");
        }
        free(str);
      } else {
        send_response("ERROR: String too long");
      }
      break;
    }
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if (nvs_get_blob(handle, key, NULL, &len) == ESP_OK) {
        snprintf(resp, sizeof(resp), "BLOB:%u", (unsigned)len);
        send_response(resp);
      } else {
        send_response("ERROR: Read failed");
      }
      break;
    }
    default:
      send_response("ERROR: Unknown type");
      break;
  }
  
  nvs_close(handle);
}

// SET <type> <key> <value> - set a value
static void settings_cmd_set(const char *args) {
  char type[16], key[64], value[384];  // Larger buffer for base64 blobs
  
  if (sscanf(args, "%15s %63s %383[^\n]", type, key, value) < 3) {
    send_response("ERROR: Usage: SET <type> <key> <value>");
    return;
  }
  
  esp_err_t err = ESP_FAIL;
  
  if (strcmp(type, "u8") == 0) {
    err = app_settings_save_u8(key, (uint8_t)atoi(value));
  } else if (strcmp(type, "u16") == 0) {
    err = app_settings_save_u16(key, (uint16_t)atoi(value));
  } else if (strcmp(type, "u32") == 0) {
    err = app_settings_save_u32(key, (uint32_t)strtoul(value, NULL, 10));
  } else if (strcmp(type, "bool") == 0) {
    bool bval = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
    err = app_settings_save_bool(key, bval);
  } else if (strcmp(type, "str") == 0) {
    err = app_settings_save_str(key, value);
  } else if (strcmp(type, "blob") == 0) {
    // Decode base64 blob
    size_t b64_len = strlen(value);
    size_t decoded_len = 0;
    
    // Calculate max decoded size (base64 is ~4/3 ratio)
    size_t max_decoded = (b64_len * 3) / 4 + 4;
    uint8_t *decoded = malloc(max_decoded);
    
    if (!decoded) {
      send_response("ERROR: Out of memory");
      return;
    }
    
    int ret = mbedtls_base64_decode(decoded, max_decoded, &decoded_len,
      (const unsigned char *)value, b64_len);
    
    if (ret != 0) {
      free(decoded);
      send_response("ERROR: Invalid base64");
      return;
    }
    
    err = app_settings_save_blob(key, decoded, decoded_len);
    free(decoded);
  } else {
    send_response("ERROR: Unknown type (use u8/u16/u32/bool/str/blob)");
    return;
  }
  
  if (err == ESP_OK) {
    send_response("OK");
  } else {
    char resp[64];
    snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
    send_response(resp);
  }
}

// ERASE <key> - erase a single key
static void settings_cmd_erase(const char *key) {
  esp_err_t err = app_settings_erase_key(key);
  if (err == ESP_OK) {
    send_response("OK");
  } else {
    char resp[64];
    snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
    send_response(resp);
  }
}

// DUMP - export all settings as JSON
static void settings_cmd_dump(void) {
  char *json = app_settings_export_json_string(false);
  if (json) {
    send_response(json);
    cJSON_free(json);
  } else {
    send_response("{}");
  }
}

// LOAD <json> - import settings from JSON
static void settings_cmd_load(const char *json_str) {
  cJSON *json = cJSON_Parse(json_str);
  if (!json) {
    send_response("ERROR: Invalid JSON");
    return;
  }
  
  int count = app_settings_import_json(json);
  cJSON_Delete(json);
  
  if (count >= 0) {
    char resp[32];
    snprintf(resp, sizeof(resp), "OK:%d", count);
    send_response(resp);
  } else {
    send_response("ERROR: Import failed");
  }
}

// Process settings mode commands
static void process_settings_command(const char *cmd) {
  CDC_LOGD("Settings cmd: %s", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave settings mode
  if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "exit") == 0) {
    CDC_LOGI("Exiting settings mode");
    s_state = CDC_STATE_IDLE;
    send_response("SETTINGS_STOPPED");
    return;
  }

  if (cdc_try_info_command(cmd)) return;
  
  // LIST - enumerate all keys
  if (strcmp(cmd, "LIST") == 0) {
    settings_cmd_list();
    return;
  }
  
  // GET <key>
  if (strncmp(cmd, "GET ", 4) == 0) {
    settings_cmd_get(cmd + 4);
    return;
  }
  
  // SET <type> <key> <value>
  if (strncmp(cmd, "SET ", 4) == 0) {
    settings_cmd_set(cmd + 4);
    return;
  }
  
  // ERASE <key>
  if (strncmp(cmd, "ERASE ", 6) == 0) {
    settings_cmd_erase(cmd + 6);
    return;
  }
  
  // ERASE_ALL
  if (strcmp(cmd, "ERASE_ALL") == 0) {
    esp_err_t err = app_settings_erase_all();
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // DUMP - export as JSON
  if (strcmp(cmd, "DUMP") == 0) {
    settings_cmd_dump();
    return;
  }
  
  // LOAD <json>
  if (strncmp(cmd, "LOAD ", 5) == 0) {
    settings_cmd_load(cmd + 5);
    return;
  }
  
  send_response("ERROR: Unknown settings command");
}

// ============================================================================
// Config Mode - Semantic settings access via settings_registry
// ============================================================================

// Buffer for VALUES JSON response (allocated in PSRAM)
#define CONFIG_VALUES_BUF_SIZE 8192

// Process config mode commands
static void process_config_command(const char *cmd) {
  CDC_LOGD("Config cmd: %s", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave config mode
  if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "exit") == 0) {
    CDC_LOGI("Exiting config mode");
    s_state = CDC_STATE_IDLE;
    send_response("CONFIG_STOPPED");
    return;
  }

  if (cdc_try_info_command(cmd)) return;
  
  // VALUES - get all setting values as JSON
  if (strcmp(cmd, "VALUES") == 0) {
    char *buf = heap_caps_malloc(CONFIG_VALUES_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) {
      send_response("ERROR: Out of memory");
      return;
    }
    
    size_t written = 0;
    esp_err_t err = settings_registry_get_all_values(buf, CONFIG_VALUES_BUF_SIZE, &written);
    if (err == ESP_OK) {
      send_response(buf);
    } else {
      send_response("ERROR: Failed to get values");
    }
    
    heap_caps_free(buf);
    return;
  }
  
  // GET <id> - get a single setting value
  if (strncmp(cmd, "GET ", 4) == 0) {
    const char *id = cmd + 4;
    uint32_t value = 0;
    
    esp_err_t err = settings_registry_get_value(id, &value);
    if (err == ESP_OK) {
      char resp[32];
      snprintf(resp, sizeof(resp), "%lu", (unsigned long)value);
      send_response(resp);
    } else if (err == ESP_ERR_NOT_FOUND) {
      send_response("ERROR: Unknown setting");
    } else {
      send_response("ERROR: Read failed");
    }
    return;
  }
  
  // SET <id> <value> - set a single setting
  if (strncmp(cmd, "SET ", 4) == 0) {
    char id[64];
    uint32_t value = 0;
    
    if (sscanf(cmd + 4, "%63s %lu", id, (unsigned long *)&value) != 2) {
      send_response("ERROR: Usage: SET <id> <value>");
      return;
    }
    
    esp_err_t err = settings_registry_set_value(id, value);
    if (err == ESP_OK) {
      send_response("OK");
    } else if (err == ESP_ERR_NOT_FOUND) {
      send_response("ERROR: Unknown setting");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // FACTORY_RESET - erase all NVS and restart
  if (strcmp(cmd, "FACTORY_RESET") == 0) {
    ESP_LOGW(TAG, "Factory reset requested via CONFIG mode");
    esp_err_t err = app_settings_erase_all();
    if (err == ESP_OK) {
      send_response("OK");
      vTaskDelay(pdMS_TO_TICKS(100));  // Give time to send response
      esp_restart();
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // COUNT - get number of registered settings
  if (strcmp(cmd, "COUNT") == 0) {
    char resp[32];
    snprintf(resp, sizeof(resp), "%u", (unsigned)settings_registry_count());
    send_response(resp);
    return;
  }
  
  send_response("ERROR: Unknown config command");
}

// ============================================================================
// Scenes Mode - Scene management via web UI
// ============================================================================

#define SCENES_JSON_BUF_SIZE 8192

// Web-initiated scene switches should apply immediately even when the device
// is configured for pending (confirm-on-device) scene changes.
static esp_err_t cdc_scene_apply_if_pending(esp_err_t err) {
  if (err == ESP_OK && scene_has_pending_change()) {
    return scene_confirm_change();
  }
  return err;
}

// Process scenes mode commands
static void process_scenes_command(const char *cmd) {
  CDC_LOGD("Scenes cmd: %s", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave scenes mode
  if (strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "exit") == 0) {
    CDC_LOGI("Exiting scenes mode");
    s_state = CDC_STATE_IDLE;
    send_response("SCENES_STOPPED");
    return;
  }

  if (cdc_try_info_command(cmd)) return;

  // SCENE_INSPECT — allowed in scenes mode (inspect uses idle handler)
  if (strncmp(cmd, "SCENE_INSPECT", 13) == 0) {
    const char *arg = cmd + 13;
    while (arg && *arg == ' ') arg++;
    cdc_send_scene_inspect(arg && *arg ? arg : NULL);
    return;
  }
  
  // LIST - get all scenes as JSON array
  if (strcmp(cmd, "LIST") == 0) {
    uint16_t total = scene_get_total_count();
    uint8_t current_idx = scene_get_current_index();

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
      send_response("ERROR: Out of memory");
      return;
    }

    for (uint16_t i = 0; i < total; i++) {
      const char *name = scene_get_name_by_position(i);
      if (!name || name[0] == '\0') name = "Untitled";
      uint8_t idx = scene_get_index_by_position(i);
      bool active = scene_is_active_by_position(i);
      bool is_current = (idx == current_idx);

      cJSON *item = cJSON_CreateObject();
      if (!item) continue;
      cJSON_AddNumberToObject(item, "position", (double)i);
      cJSON_AddNumberToObject(item, "index", (double)idx);
      cJSON_AddStringToObject(item, "name", name);
      cJSON_AddBoolToObject(item, "active", active);
      cJSON_AddBoolToObject(item, "current", is_current);
      cJSON_AddBoolToObject(item, "factory", scene_is_factory_by_position(i));
      cJSON_AddItemToArray(arr, item);
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
      send_response("ERROR: Out of memory");
      return;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    send_response(json);
    cJSON_free(json);
    return;
  }
  
  // CURRENT - get current scene position
  if (strcmp(cmd, "CURRENT") == 0) {
    uint8_t current_idx = scene_get_current_index();
    uint16_t total = scene_get_total_count();
    
    // Find position of current scene
    for (uint16_t i = 0; i < total; i++) {
      if (scene_get_index_by_position(i) == current_idx) {
        char resp[16];
        snprintf(resp, sizeof(resp), "%u", (unsigned)i);
        send_response(resp);
        return;
      }
    }
    send_response("ERROR: Current scene not found");
    return;
  }

  // GOTO <position> - switch to scene at manifest position
  if (strncmp(cmd, "GOTO ", 5) == 0) {
    unsigned pos;
    if (sscanf(cmd + 5, "%u", &pos) != 1) {
      send_response("ERROR: Usage: GOTO <position>");
      return;
    }

    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    if (!scene_is_active(idx)) {
      send_response("ERROR: Scene inactive");
      return;
    }

    esp_err_t err = cdc_scene_apply_if_pending(scene_set_current(idx));
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // REORDER <from> <to> - move scene from position to position
  if (strncmp(cmd, "REORDER ", 8) == 0) {
    unsigned from_pos, to_pos;
    if (sscanf(cmd + 8, "%u %u", &from_pos, &to_pos) != 2) {
      send_response("ERROR: Usage: REORDER <from> <to>");
      return;
    }
    
    uint8_t from_idx = scene_get_index_by_position((uint16_t)from_pos);
    uint8_t to_idx = scene_get_index_by_position((uint16_t)to_pos);
    
    esp_err_t err = scene_reorder(from_idx, to_idx);
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // RENAME <position> <name> - rename scene at position
  if (strncmp(cmd, "RENAME ", 7) == 0) {
    unsigned pos;
    
    // Parse position and name (name may contain spaces)
    const char *args = cmd + 7;
    char *endptr;
    pos = strtoul(args, &endptr, 10);
    
    if (endptr == args || *endptr != ' ') {
      send_response("ERROR: Usage: RENAME <position> <name>");
      return;
    }
    
    // Skip space and get name
    const char *new_name = endptr + 1;
    if (strlen(new_name) == 0 || strlen(new_name) > 16) {
      send_response("ERROR: Name must be 1-16 characters");
      return;
    }
    
    if (scene_name_is_reserved(new_name)) {
      send_response("ERROR: Reserved name");
      return;
    }

    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    CDC_LOGI("RENAME: calling scene_set_name for index %d", idx);
    int64_t start = esp_timer_get_time();
    esp_err_t err = scene_set_name(idx, new_name);
    int64_t elapsed = (esp_timer_get_time() - start) / 1000;
    CDC_LOGI("RENAME: scene_set_name completed in %lld ms, result=%s", elapsed, esp_err_to_name(err));
    
    // Yield to let USB task service any pending data after file I/O
    vTaskDelay(pdMS_TO_TICKS(10));
    
    if (err == ESP_OK) {
      send_response("OK");
    } else if (err == ESP_ERR_INVALID_ARG) {
      send_response("ERROR: Name already exists");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // DELETE <position> - delete scene at position
  if (strncmp(cmd, "DELETE ", 7) == 0) {
    unsigned pos;
    if (sscanf(cmd + 7, "%u", &pos) != 1) {
      send_response("ERROR: Usage: DELETE <position>");
      return;
    }
    
    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    esp_err_t err = scene_delete(idx);
    
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // DUPLICATE <position> <name> - duplicate scene at position with given name
  if (strncmp(cmd, "DUPLICATE ", 10) == 0) {
    unsigned pos;
    
    // Parse position and name (name may contain spaces)
    const char *args = cmd + 10;
    char *endptr;
    pos = strtoul(args, &endptr, 10);
    
    if (endptr == args || *endptr != ' ') {
      send_response("ERROR: Usage: DUPLICATE <position> <name>");
      return;
    }
    
    // Skip space and get name
    const char *new_name = endptr + 1;
    if (strlen(new_name) == 0 || strlen(new_name) > 16) {
      send_response("ERROR: Name must be 1-16 characters");
      return;
    }
    
    // Check for duplicate name
    if (scene_name_exists(new_name, -1)) {
      send_response("ERROR: Name already exists");
      return;
    }
    if (scene_name_is_reserved(new_name)) {
      send_response("ERROR: Reserved name");
      return;
    }
    
    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    esp_err_t err = scene_duplicate(idx, new_name);
    
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // ACTIVATE <position> - set scene active
  if (strncmp(cmd, "ACTIVATE ", 9) == 0) {
    unsigned pos;
    if (sscanf(cmd + 9, "%u", &pos) != 1) {
      send_response("ERROR: Usage: ACTIVATE <position>");
      return;
    }
    
    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    esp_err_t err = scene_set_active(idx, true);
    
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // DEACTIVATE <position> - set scene inactive
  if (strncmp(cmd, "DEACTIVATE ", 11) == 0) {
    unsigned pos;
    if (sscanf(cmd + 11, "%u", &pos) != 1) {
      send_response("ERROR: Usage: DEACTIVATE <position>");
      return;
    }
    
    uint8_t idx = scene_get_index_by_position((uint16_t)pos);
    esp_err_t err = scene_set_active(idx, false);
    
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  // CREATE <name> - create new empty scene
  if (strncmp(cmd, "CREATE ", 7) == 0) {
    const char *name = cmd + 7;
    
    if (strlen(name) == 0 || strlen(name) > 16) {
      send_response("ERROR: Name must be 1-16 characters");
      return;
    }
    
    if (scene_name_exists(name, -1)) {
      send_response("ERROR: Name already exists");
      return;
    }
    if (scene_name_is_reserved(name)) {
      send_response("ERROR: Reserved name");
      return;
    }
    
    esp_err_t err = scene_create_new(name);
    
    if (err == ESP_OK) {
      send_response("OK");
    } else {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
    }
    return;
  }
  
  send_response("ERROR: Unknown scenes command");
}

// ============================================================================
// PEDALS MODE - Pedal database browsing
// ============================================================================

// Process pedals mode commands
static void process_pedals_command(const char *cmd) {
  CDC_LOGI("Pedals cmd: '%s'", cmd);
  
  if (strlen(cmd) == 0) return;
  
  // EXIT - leave pedals mode
  if (strcmp(cmd, "EXIT") == 0) {
    CDC_LOGI("Exiting pedals mode");
    s_state = CDC_STATE_IDLE;
    send_response("PEDALS_STOPPED");
    return;
  }

  if (cdc_try_info_command(cmd)) return;

  if (cdc_try_pedal_set_command(cmd)) return;
  
  // MANIFEST - send the full manifest.json file
  if (strcmp(cmd, "MANIFEST") == 0) {
    const char *manifest_path = ASSETS_BASE_PATH "/devices/manifest.json";
    
    struct stat st;
    if (stat(manifest_path, &st) != 0) {
      send_response("ERROR: Manifest not found");
      return;
    }
    
    FILE *f = fopen(manifest_path, "r");
    if (!f) {
      send_response("ERROR: Failed to open manifest");
      return;
    }
    
    // Send size header
    char resp[32];
    snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)st.st_size);
    send_response(resp);
    
    // Send file content in chunks
    uint8_t chunk[512];
    size_t sent = 0;
    while (sent < (size_t)st.st_size) {
      size_t to_read = sizeof(chunk);
      if (sent + to_read > (size_t)st.st_size) {
        to_read = (size_t)st.st_size - sent;
      }
      size_t read = fread(chunk, 1, to_read, f);
      if (read == 0) break;
      send_binary(chunk, read);
      sent += read;
    }
    if (st.st_size > 0 && ((size_t)st.st_size % 64) == 0) {
      uint8_t term = '\n';
      send_binary(&term, 1);
    }
    fclose(f);
    return;
  }
  
  // LOAD <slug> - load a specific device JSON file
  if (strncmp(cmd, "LOAD ", 5) == 0) {
    const char *slug = cmd + 5;
    
    // Look up the device in manifest to get the file path
    const manifest_device_t *mdev = assets_get_manifest_device(slug);
    if (!mdev) {
      send_response("ERROR: Device not found in manifest");
      return;
    }
    
    // Check if file path is populated
    if (mdev->file[0] == '\0') {
      ESP_LOGE(TAG, "Device %s has empty file path", slug);
      send_response("ERROR: Device has no file path");
      return;
    }
    
    char full_path[MAX_PATH_LEN];
    if (assets_manifest_device_json_path(mdev, full_path, sizeof(full_path)) != ESP_OK) {
      send_response("ERROR: Device has no file path");
      return;
    }
    CDC_LOGI("Loading device file: %s", full_path);
    
    struct stat st;
    if (stat(full_path, &st) != 0) {
      ESP_LOGE(TAG, "Device file not found: %s (errno=%d)", full_path, errno);
      send_response("ERROR: Device file not found");
      return;
    }
    
    FILE *f = fopen(full_path, "r");
    if (!f) {
      send_response("ERROR: Failed to open device file");
      return;
    }
    
    // Send size header
    char resp[32];
    snprintf(resp, sizeof(resp), "SIZE %u", (unsigned)st.st_size);
    send_response(resp);
    
    // Send file content in chunks
    uint8_t chunk[512];
    size_t sent = 0;
    while (sent < (size_t)st.st_size) {
      size_t to_read = sizeof(chunk);
      if (sent + to_read > (size_t)st.st_size) {
        to_read = (size_t)st.st_size - sent;
      }
      size_t read = fread(chunk, 1, to_read, f);
      if (read == 0) break;
      send_binary(chunk, read);
      sent += read;
    }
    if (st.st_size > 0 && ((size_t)st.st_size % 64) == 0) {
      uint8_t term = '\n';
      send_binary(&term, 1);
    }
    fclose(f);
    return;
  }
  
  // SELECT <slug> - select a device as the current pedal
  if (strncmp(cmd, "SELECT ", 7) == 0) {
    const char *slug = cmd + 7;
    
    // Verify the device exists
    const manifest_device_t *mdev = assets_get_manifest_device(slug);
    if (!mdev) {
      send_response("ERROR: Device not found");
      return;
    }
    
    // Set the pedal
    esp_err_t err = device_config_set_pedal(slug);
    if (err != ESP_OK) {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: %s", esp_err_to_name(err));
      send_response(resp);
      return;
    }
    
    // Save configuration
    err = device_config_save();
    if (err != ESP_OK) {
      char resp[64];
      snprintf(resp, sizeof(resp), "ERROR: Save failed - %s", esp_err_to_name(err));
      send_response(resp);
      return;
    }
    
    send_response("OK");
    return;
  }
  
  send_response("ERROR: Unknown pedals command");
}
