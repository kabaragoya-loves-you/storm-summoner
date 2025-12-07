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
#define STREAM_QUEUE_SIZE     2      // Number of pending flush items
#define STREAM_TASK_STACK   2048     // TX task stack size
#define STREAM_TASK_PRIO      5      // TX task priority
#define BYTES_PER_PIXEL       3      // RGB888

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
  ESP_LOGI(TAG, "LVGL stream initialized");
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
    xSemaphoreGive(s_mutex);
    return ESP_ERR_NO_MEM;
  }

  s_streaming = true;
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Streaming started");
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
  ESP_LOGI(TAG, "Streaming stopped");
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
      .format = LVGL_STREAM_FMT_RGB888,
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
    ESP_LOGD(TAG, "Queue full, dropping frame");
    return ESP_ERR_NO_MEM;
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

  ESP_LOGI(TAG, "TX task started");

  while (s_streaming) {
    // Wait for item with timeout so we can check s_streaming flag
    if (xQueueReceive(s_tx_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
      // Check CDC is connected
      if (tud_cdc_n_connected(0)) {
        send_header_and_pixels(&item.header, item.pixels);
      }
      // Free pixel buffer
      heap_caps_free(item.pixels);
    }
  }

  ESP_LOGI(TAG, "TX task exiting");
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
