#include "compressed_loader.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

// Suppress warnings from miniz header's unused static functions
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// Use local miniz component (has full mz_uncompress API)
// Path includes "miniz/" to avoid conflict with ESP-IDF's esp_rom/miniz.h
#include "miniz/miniz.h"

#pragma GCC diagnostic pop

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "COMPRESS"

void *compressed_load(const char *path, size_t *out_size) {
  if (!path || !out_size) return NULL;
  
  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", path);
    return NULL;
  }
  
  // Get file size
  fseek(f, 0, SEEK_END);
  long file_size = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  if (file_size < 5) {  // Minimum: 4 byte header + 1 byte data
    ESP_LOGE(TAG, "File too small: %ld bytes", file_size);
    fclose(f);
    return NULL;
  }
  
  // Read original size from header
  uint32_t original_size;
  if (fread(&original_size, sizeof(original_size), 1, f) != 1) {
    ESP_LOGE(TAG, "Failed to read header");
    fclose(f);
    return NULL;
  }
  
  size_t compressed_size = file_size - 4;
  
  ESP_LOGI(TAG, "Loading %s: %lu compressed -> %lu bytes", 
           path, (unsigned long)compressed_size, (unsigned long)original_size);
  
  // Allocate buffer for compressed data (can use regular heap, temporary)
  uint8_t *compressed = (uint8_t *)malloc(compressed_size);
  if (!compressed) {
    ESP_LOGE(TAG, "Failed to allocate %lu bytes for compressed data", (unsigned long)compressed_size);
    fclose(f);
    return NULL;
  }
  
  // Read compressed data
  if (fread(compressed, 1, compressed_size, f) != compressed_size) {
    ESP_LOGE(TAG, "Failed to read compressed data");
    free(compressed);
    fclose(f);
    return NULL;
  }
  fclose(f);
  
  // Allocate PSRAM for decompressed data
  uint8_t *decompressed = (uint8_t *)heap_caps_malloc(original_size, MALLOC_CAP_SPIRAM);
  if (!decompressed) {
    ESP_LOGE(TAG, "Failed to allocate %lu bytes in PSRAM", (unsigned long)original_size);
    free(compressed);
    return NULL;
  }
  
  // Decompress using miniz
  mz_ulong dest_len = original_size;
  int result = mz_uncompress(decompressed, &dest_len, compressed, compressed_size);
  
  // Free compressed buffer (no longer needed)
  free(compressed);
  
  if (result != MZ_OK) {
    ESP_LOGE(TAG, "Decompression failed: %d", result);
    heap_caps_free(decompressed);
    return NULL;
  }
  
  if (dest_len != original_size) {
    ESP_LOGW(TAG, "Size mismatch: expected %lu, got %lu", 
             (unsigned long)original_size, (unsigned long)dest_len);
  }
  
  *out_size = dest_len;
  ESP_LOGI(TAG, "Decompressed %lu bytes to PSRAM", (unsigned long)dest_len);
  
  return decompressed;
}

void compressed_free(void *data) {
  if (data) {
    heap_caps_free(data);
  }
}
