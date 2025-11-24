#include "usb_cdc_update.h"
#include "firmware_update.h"
#include "tusb.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include "esp_system.h"

#define TAG "USB_CDC_UPDATE"

#define CDC_RX_BUF_SIZE 1024
#define CDC_CMD_BUF_SIZE 128

// Update protocol states
typedef enum {
  CDC_STATE_IDLE,
  CDC_STATE_RECEIVING_FIRMWARE,
  CDC_STATE_RECEIVING_ASSETS,
  CDC_STATE_WAITING_COMMIT, // New state after transfer complete
  CDC_STATE_COMMITTING,
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

// Forward declarations
static void process_command(const char *cmd);
static void send_response(const char *msg);
static void handle_binary_data(const uint8_t *data, size_t len);

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

  // CDC is initialized by the composite descriptor
  // No additional initialization needed - TinyUSB handles it

  // Create task to poll CDC
  xTaskCreate(cdc_update_task, "cdc_update", 4096, NULL, 5, NULL);

  s_initialized = true;
  ESP_LOGI(TAG, "CDC update handler initialized");
  return ESP_OK;
}

void usb_cdc_task(void) {
  if (!s_initialized) return;

  // Check if CDC is connected
  if (!tud_cdc_n_connected(0)) {
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

