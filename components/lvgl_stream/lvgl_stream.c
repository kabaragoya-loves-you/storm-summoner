#include "lvgl_stream.h"
#include "tusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

#define TAG "LVGL_STREAM"

// Queue configuration
#define STREAM_QUEUE_SIZE    16      // Number of pending flush items (PSRAM buffers)
#define SYNC_DEBOUNCE_MS    100      // Wait this long after last drop before auto-sync
#define SYNC_MAX_INTERVAL_MS 5000    // Force sync if this long since last sync (handles continuous drops)
#define STREAM_TASK_STACK  4096      // TX task stack size
#define STREAM_TASK_PRIO      5      // TX task priority
#define BYTES_PER_PIXEL       2      // RGB565

// Queue item: header + pointer to pixel data in PSRAM
typedef struct {
  lvgl_stream_hdr_t header;
  uint8_t *pixels;  // Allocated in PSRAM, freed after send
} stream_queue_item_t;

// Module state
static bool s_initialized = false;
static bool s_streaming = false;
static QueueHandle_t s_tx_queue = NULL;
static TaskHandle_t s_tx_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;

// Display dimensions (cached from display driver)
static uint16_t s_display_width = 0;
static uint16_t s_display_height = 0;

// Statistics
static uint32_t s_frames_sent = 0;
static uint32_t s_frames_dropped = 0;
static uint32_t s_bytes_sent = 0;
static uint32_t s_max_queue_depth = 0;      // High-water mark

// Sync request flag (set by SYNC command, cleared after full redraw)
static volatile bool s_sync_requested = false;

// Deferred sync after drops - debounce pattern
static TickType_t s_last_drop_tick = 0;     // Tick count of last dropped frame
static TickType_t s_last_sync_tick = 0;     // Tick count of last completed sync
static uint32_t s_drops_since_sync = 0;     // Drops since last sync completed

// Forward declarations
static void stream_tx_task(void *arg);
static void send_header_and_pixels(const lvgl_stream_hdr_t *hdr, const uint8_t *pixels);

esp_err_t lvgl_stream_init(void) {
  if (s_initialized) return ESP_OK;

  // Create mutex for state protection
  s_mutex = xSemaphoreCreateMutex();
  if (!s_mutex) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_ERR_NO_MEM;
  }

  // Create TX queue
  s_tx_queue = xQueueCreate(STREAM_QUEUE_SIZE, sizeof(stream_queue_item_t));
  if (!s_tx_queue) {
    ESP_LOGE(TAG, "Failed to create TX queue");
    vSemaphoreDelete(s_mutex);
    return ESP_ERR_NO_MEM;
  }

  s_initialized = true;
  ESP_LOGD(TAG, "LVGL stream initialized");
  return ESP_OK;
}

void lvgl_stream_set_dimensions(uint16_t width, uint16_t height) {
  s_display_width = width;
  s_display_height = height;
  ESP_LOGI(TAG, "Stream dimensions set: %dx%d", width, height);
}

esp_err_t lvgl_stream_start(void) {
  if (!s_initialized) {
    ESP_LOGE(TAG, "Not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);

  if (s_streaming) {
    xSemaphoreGive(s_mutex);
    return ESP_OK;  // Already streaming
  }

  // Log heap stats before task creation
  ESP_LOGI(TAG, "Free heap: %u, largest block: %u, free internal: %u",
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  // Set streaming flag BEFORE creating task to avoid race condition
  // (task checks s_streaming immediately on start)
  s_streaming = true;

  // Create TX task
  BaseType_t ret = xTaskCreate(
    stream_tx_task,
    "lvgl_stream_tx",
    STREAM_TASK_STACK,
    NULL,
    STREAM_TASK_PRIO,
    &s_tx_task
  );

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create TX task, ret=%d", (int)ret);
    s_streaming = false;  // Rollback on failure
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NO_MEM;
  }

  xSemaphoreGive(s_mutex);

  ESP_LOGD(TAG, "Streaming started");
  return ESP_OK;
}

void lvgl_stream_stop(void) {
  if (!s_initialized) return;

  xSemaphoreTake(s_mutex, portMAX_DELAY);

  if (!s_streaming) {
    xSemaphoreGive(s_mutex);
    return;
  }

  s_streaming = false;

  // Signal task to exit and wait for it
  if (s_tx_task) {
    // Give task time to notice s_streaming is false
    xSemaphoreGive(s_mutex);
    vTaskDelay(pdMS_TO_TICKS(50));
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Task should have exited, but delete if still running
    if (s_tx_task) {
      vTaskDelete(s_tx_task);
      s_tx_task = NULL;
    }
  }

  // Drain and free any remaining queue items
  stream_queue_item_t item;
  while (xQueueReceive(s_tx_queue, &item, 0) == pdTRUE) {
    if (item.pixels) heap_caps_free(item.pixels);
  }

  xSemaphoreGive(s_mutex);
  ESP_LOGD(TAG, "Streaming stopped");
}

bool lvgl_stream_is_active(void) {
  return s_streaming;
}

esp_err_t lvgl_stream_queue_flush(const lv_area_t *area, const uint8_t *px_map) {
  if (!s_streaming) return ESP_ERR_INVALID_STATE;

  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  size_t payload_size = w * h * BYTES_PER_PIXEL;

  // Allocate pixel buffer in PSRAM
  uint8_t *pixels = heap_caps_malloc(payload_size, MALLOC_CAP_SPIRAM);
  if (!pixels) {
    ESP_LOGW(TAG, "Failed to allocate %u bytes for stream", (unsigned)payload_size);
    return ESP_ERR_NO_MEM;
  }

  // Copy pixel data
  memcpy(pixels, px_map, payload_size);

  // Build queue item
  stream_queue_item_t item = {
    .header = {
      .magic = LVGL_STREAM_MAGIC,
      .type = LVGL_STREAM_TYPE_RECT,
      .format = LVGL_STREAM_FMT_RGB565,
      .x = (uint16_t)area->x1,
      .y = (uint16_t)area->y1,
      .w = (uint16_t)w,
      .h = (uint16_t)h,
      .payload_len = (uint32_t)payload_size
    },
    .pixels = pixels
  };

  // Try to queue (don't block - drop if full)
  if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
    heap_caps_free(pixels);
    s_frames_dropped++;
    s_drops_since_sync++;
    s_last_drop_tick = xTaskGetTickCount();
    // Log periodically to avoid spam
    if ((s_frames_dropped & 0x1F) == 1) {
      ESP_LOGD(TAG, "Queue full, dropped %u frames total", (unsigned)s_frames_dropped);
    }
    return ESP_ERR_NO_MEM;
  }

  // Track queue depth high-water mark
  UBaseType_t current_depth = uxQueueMessagesWaiting(s_tx_queue);
  if (current_depth > s_max_queue_depth) {
    s_max_queue_depth = current_depth;
    ESP_LOGD(TAG, "Queue high-water mark: %u/%d", (unsigned)s_max_queue_depth, STREAM_QUEUE_SIZE);
  }

  return ESP_OK;
}

void lvgl_stream_get_dimensions(uint16_t *width, uint16_t *height) {
  if (width) *width = s_display_width;
  if (height) *height = s_display_height;
}

// TX task: drains queue and sends over CDC
static void stream_tx_task(void *arg) {
  (void)arg;
  stream_queue_item_t item;

  ESP_LOGD(TAG, "TX task started");

  while (s_streaming) {
    // Wait for item with timeout so we can check s_streaming flag
    if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      // Check CDC is connected
      if (tud_cdc_n_connected(0)) {
        send_header_and_pixels(&item.header, item.pixels);
        s_frames_sent++;
        s_bytes_sent += sizeof(lvgl_stream_hdr_t) + item.header.payload_len;
      }
      // Free pixel buffer
      heap_caps_free(item.pixels);
    }
  }

  ESP_LOGI(TAG, "TX task exiting: sent=%u dropped=%u bytes=%u max_queue=%u/%d",
    (unsigned)s_frames_sent, (unsigned)s_frames_dropped, 
    (unsigned)s_bytes_sent, (unsigned)s_max_queue_depth, STREAM_QUEUE_SIZE);
  s_tx_task = NULL;
  vTaskDelete(NULL);
}

// Send header + pixels over CDC with chunking
static void send_header_and_pixels(const lvgl_stream_hdr_t *hdr, const uint8_t *pixels) {
  const size_t chunk_size = 512;
  const int max_retries = 50;

  // Send header
  size_t hdr_size = sizeof(lvgl_stream_hdr_t);
  size_t sent = 0;
  int retries = 0;

  while (sent < hdr_size && retries < max_retries) {
    if (!tud_cdc_n_connected(0)) return;

    uint32_t written = tud_cdc_n_write(0, ((const uint8_t *)hdr) + sent, hdr_size - sent);
    tud_cdc_n_write_flush(0);
    sent += written;

    if (written == 0) {
      retries++;
      vTaskDelay(pdMS_TO_TICKS(2));
    } else {
      retries = 0;
    }
  }

  // Send pixel payload in chunks
  size_t payload_len = hdr->payload_len;
  sent = 0;
  retries = 0;

  while (sent < payload_len && retries < max_retries) {
    if (!tud_cdc_n_connected(0)) return;

    size_t to_send = (payload_len - sent > chunk_size) ? chunk_size : (payload_len - sent);
    uint32_t written = tud_cdc_n_write(0, pixels + sent, to_send);
    tud_cdc_n_write_flush(0);
    sent += written;

    if (written == 0) {
      retries++;
      vTaskDelay(pdMS_TO_TICKS(2));
    } else {
      retries = 0;
    }
  }
}

void lvgl_stream_request_sync(void) {
  if (!s_streaming) return;
  s_sync_requested = true;
  ESP_LOGD(TAG, "Sync requested");
}

bool lvgl_stream_sync_pending(void) {
  return s_sync_requested;
}

void lvgl_stream_sync_done(void) {
  s_sync_requested = false;
  s_drops_since_sync = 0;  // Reset drop counter after sync completes
  s_last_sync_tick = xTaskGetTickCount();
}

// Check if deferred sync should trigger (called from lvgl_task)
bool lvgl_stream_needs_deferred_sync(void) {
  if (!s_streaming || s_drops_since_sync == 0) return false;
  
  TickType_t now = xTaskGetTickCount();
  TickType_t since_drop = now - s_last_drop_tick;
  TickType_t since_sync = now - s_last_sync_tick;
  
  // Debounce: sync after drops have stopped for a bit
  if (since_drop >= pdMS_TO_TICKS(SYNC_DEBOUNCE_MS)) {
    ESP_LOGD(TAG, "Deferred sync after %u drops (debounce)", (unsigned)s_drops_since_sync);
    return true;
  }
  
  // Fallback: if drops are continuous, sync periodically anyway
  if (since_sync >= pdMS_TO_TICKS(SYNC_MAX_INTERVAL_MS)) {
    ESP_LOGD(TAG, "Periodic sync after %u drops (max interval)", (unsigned)s_drops_since_sync);
    return true;
  }
  
  return false;
}

void lvgl_stream_get_stats(uint32_t *sent, uint32_t *dropped, uint32_t *bytes) {
  if (sent) *sent = s_frames_sent;
  if (dropped) *dropped = s_frames_dropped;
  if (bytes) *bytes = s_bytes_sent;
}

uint32_t lvgl_stream_get_max_queue_depth(void) {
  return s_max_queue_depth;
}

uint32_t lvgl_stream_get_current_queue_depth(void) {
  if (!s_tx_queue) return 0;
  return (uint32_t)uxQueueMessagesWaiting(s_tx_queue);
}

void lvgl_stream_reset_stats(void) {
  s_frames_sent = 0;
  s_frames_dropped = 0;
  s_bytes_sent = 0;
  s_max_queue_depth = 0;
}
