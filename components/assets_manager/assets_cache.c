#include "assets_manager.h"
#include "assets_types.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "rom/crc.h"
#include <string.h>
#include <sys/stat.h>

#define TAG "assets_cache"

// Helper: try PSRAM first, fall back to regular heap
static void *malloc_prefer_psram(size_t size) {
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!ptr) {
    ESP_LOGD(TAG, "PSRAM unavailable, using regular heap for %u bytes", (unsigned)size);
    ptr = malloc(size);
  }
  return ptr;
}

static void *calloc_prefer_psram(size_t count, size_t size) {
  void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM);
  if (!ptr) {
    ESP_LOGD(TAG, "PSRAM unavailable, using regular heap for %u bytes", (unsigned)(count * size));
    ptr = calloc(count, size);
  }
  return ptr;
}

static void free_smart(void *ptr) {
  if (ptr)
    heap_caps_free(ptr);
}

/**
 * Calculate CRC32 for cache validation
 */
static uint32_t calculate_crc32(const uint8_t *data, size_t len) {
  return crc32_le(0, data, len);
}

/**
 * Generate binary cache from parsed device definition
 */
esp_err_t generate_device_cache(const device_def_t *device, const char *cache_path) {
  ESP_LOGI(TAG, "Generating cache: %s", cache_path);
  
  // Open cache file for writing
  FILE *f = fopen(cache_path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create cache file: %s", cache_path);
    return ESP_FAIL;
  }
  
  // Prepare header (CRC will be calculated later)
  cache_header_t header = {
    .magic = CACHE_MAGIC,
    .schema = CACHE_SCHEMA_VERSION,
    .reserved = 0,
    .control_count = device->control_count,
    .string_blob_size = device->string_blob_size,
    .pc_name_count = 0,
    .crc32 = 0  // Will be calculated at the end
  };
  
  // TODO: SHA256 calculation would go here
  memset(header.json_sha256, 0, 32);
  
  // Count PC names
  if (device->pc_info && device->pc_info->names) {
    header.pc_name_count = device->pc_info->count;
  }
  
  // Write header (will update CRC later)
  size_t header_pos = ftell(f);
  fwrite(&header, sizeof(header), 1, f);
  
  // Write control records
  for (uint16_t i = 0; i < device->control_count; i++) {
    const midi_control_t *ctrl = &device->controls[i];
    control_record_t rec = {0};
    
    rec.type = ctrl->type;
    rec.id = ctrl->id;
    rec.min = ctrl->min;
    rec.max = ctrl->max;
    rec.flags = ctrl->flags;
    
    // Calculate offsets into string blob
    if (ctrl->name && device->string_blob) {
      rec.name_offset = (uint32_t)((const char *)ctrl->name - (const char *)device->string_blob);
    }
    if (ctrl->additional_info && device->string_blob) {
      rec.info_offset = (uint32_t)((const char *)ctrl->additional_info - (const char *)device->string_blob);
    }
    
    fwrite(&rec, sizeof(rec), 1, f);
  }
  
  // Write string blob
  if (device->string_blob && device->string_blob_size > 0) {
    fwrite(device->string_blob, 1, device->string_blob_size, f);
  }
  
  // Write PC info if present
  if (device->pc_info && device->pc_info->names) {
    // Write PC names from string blob
    for (uint16_t i = 0; i < header.pc_name_count; i++) {
      if (device->pc_info->names[i]) {
        size_t len = strlen(device->pc_info->names[i]) + 1;
        fwrite(device->pc_info->names[i], 1, len, f);
      }
    }
  }
  
  // Calculate CRC32 of all data after header
  size_t data_size = ftell(f) - sizeof(header);
  fseek(f, sizeof(header), SEEK_SET);
  
  uint8_t *data_buf = malloc(data_size);
  if (data_buf) {
    fread(data_buf, 1, data_size, f);
    header.crc32 = calculate_crc32(data_buf, data_size);
    free(data_buf);
    
    // Update header with CRC
    fseek(f, header_pos, SEEK_SET);
    fwrite(&header, sizeof(header), 1, f);
  }
  
  fclose(f);
  ESP_LOGI(TAG, "Cache generated successfully");
  return ESP_OK;
}

/**
 * Load device from binary cache
 */
device_def_t *load_device_cache(const char *cache_path, const char *slug) {
  ESP_LOGI(TAG, "Loading cache: %s", cache_path);
  
  // Check if cache file exists
  struct stat st;
  if (stat(cache_path, &st) != 0) {
    ESP_LOGD(TAG, "Cache file not found: %s", cache_path);
    return NULL;
  }
  
  // Open cache file
  FILE *f = fopen(cache_path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open cache file: %s", cache_path);
    return NULL;
  }
  
  // Read and validate header
  cache_header_t header;
  if (fread(&header, sizeof(header), 1, f) != 1) {
    ESP_LOGE(TAG, "Failed to read cache header");
    fclose(f);
    return NULL;
  }
  
  // Validate magic
  if (header.magic != CACHE_MAGIC) {
    ESP_LOGW(TAG, "Invalid cache magic: 0x%08lx", (unsigned long)header.magic);
    fclose(f);
    return NULL;
  }
  
  // Validate schema version
  if (header.schema != CACHE_SCHEMA_VERSION) {
    ESP_LOGW(TAG, "Cache schema mismatch: %u (expected %u)", (unsigned)header.schema, (unsigned)CACHE_SCHEMA_VERSION);
    fclose(f);
    return NULL;
  }
  
  // Calculate data size
  size_t control_data_size = header.control_count * sizeof(control_record_t);
  size_t total_data_size = control_data_size + header.string_blob_size;
  
  // Allocate buffer for data in PSRAM
  uint8_t *data_buf = malloc_prefer_psram(total_data_size);
  if (!data_buf) {
    ESP_LOGE(TAG, "Failed to allocate %u bytes for cache data", (unsigned)total_data_size);
    fclose(f);
    return NULL;
  }
  
  // Read all data
  if (fread(data_buf, 1, total_data_size, f) != total_data_size) {
    ESP_LOGE(TAG, "Failed to read cache data");
    free_smart(data_buf);
    fclose(f);
    return NULL;
  }
  
  // Validate CRC32
  uint32_t calculated_crc = calculate_crc32(data_buf, total_data_size);
  if (calculated_crc != header.crc32) {
    ESP_LOGW(TAG, "Cache CRC mismatch: 0x%08lx vs 0x%08lx", (unsigned long)calculated_crc, (unsigned long)header.crc32);
    free_smart(data_buf);
    fclose(f);
    return NULL;
  }
  
  fclose(f);
  
  // Allocate device structure
  device_def_t *device = calloc_prefer_psram(1, sizeof(device_def_t));
  if (!device) {
    ESP_LOGE(TAG, "Failed to allocate device_def_t");
    free_smart(data_buf);
    return NULL;
  }
  
  strncpy(device->slug, slug, sizeof(device->slug) - 1);
  device->control_count = header.control_count;
  device->string_blob_size = header.string_blob_size;
  
  // Allocate controls array
  device->controls = calloc_prefer_psram(device->control_count, sizeof(midi_control_t));
  if (!device->controls) {
    ESP_LOGE(TAG, "Failed to allocate controls array");
    free_smart(data_buf);
    free_smart(device);
    return NULL;
  }
  
  // Allocate string blob
  device->string_blob = malloc_prefer_psram(device->string_blob_size);
  if (!device->string_blob) {
    ESP_LOGE(TAG, "Failed to allocate string blob");
    free_smart(device->controls);
    free_smart(data_buf);
    free_smart(device);
    return NULL;
  }
  
  // Copy string blob
  memcpy(device->string_blob, data_buf + control_data_size, device->string_blob_size);
  
  // Parse control records
  control_record_t *records = (control_record_t *)data_buf;
  for (uint16_t i = 0; i < device->control_count; i++) {
    midi_control_t *ctrl = &device->controls[i];
    control_record_t *rec = &records[i];
    
    ctrl->type = rec->type;
    ctrl->id = rec->id;
    ctrl->min = rec->min;
    ctrl->max = rec->max;
    ctrl->flags = rec->flags;
    
    // Set name pointer
    if (rec->name_offset < device->string_blob_size)
      ctrl->name = (const char *)device->string_blob + rec->name_offset;
    
    // Set info pointer
    if (rec->info_offset > 0 && rec->info_offset < device->string_blob_size)
      ctrl->additional_info = (const char *)device->string_blob + rec->info_offset;
  }
  
  // Build CC lookup table
  device->cc_lookup = malloc_prefer_psram(128 * sizeof(int16_t));
  if (device->cc_lookup) {
    for (int i = 0; i < 128; i++)
      device->cc_lookup[i] = -1;
    
    for (uint16_t i = 0; i < device->control_count; i++) {
      if (device->controls[i].type == MIDI_CONTROL_TYPE_CC && device->controls[i].id < 128)
        device->cc_lookup[device->controls[i].id] = i;
    }
  }
  
  free_smart(data_buf);
  
  ESP_LOGI(TAG, "Cache loaded successfully: %u controls", (unsigned)device->control_count);
  return device;
}

