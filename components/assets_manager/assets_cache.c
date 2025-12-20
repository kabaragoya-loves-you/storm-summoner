#include "assets_manager.h"
#include "assets_types.h"
#include "memory_utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "rom/crc.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG "assets_cache"

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
  
  // Ensure cache directory exists
  // Extract directory from cache_path (e.g., "/assets/cache" from "/assets/cache/foo.bin")
  char dir_path[128];
  strncpy(dir_path, cache_path, sizeof(dir_path) - 1);
  dir_path[sizeof(dir_path) - 1] = '\0';
  char *last_slash = strrchr(dir_path, '/');
  if (last_slash) {
    *last_slash = '\0';
    struct stat st;
    if (stat(dir_path, &st) != 0) {
      // Directory doesn't exist, create it
      if (mkdir(dir_path, 0755) != 0) {
        ESP_LOGW(TAG, "Failed to create cache directory: %s", dir_path);
      } else {
        ESP_LOGI(TAG, "Created cache directory: %s", dir_path);
      }
    }
  }
  
  // Open cache file for writing
  FILE *f = fopen(cache_path, "wb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create cache file: %s", cache_path);
    return ESP_FAIL;
  }
  
  // Count total discrete values
  uint32_t total_discrete_count = 0;
  for (uint16_t i = 0; i < device->control_count; i++) {
    total_discrete_count += device->controls[i].discrete_count;
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
    rec.discrete_count = ctrl->discrete_count;
    
    // Calculate offsets into string blob
    if (ctrl->name && device->string_blob) {
      rec.name_offset = (uint32_t)((const char *)ctrl->name - (const char *)device->string_blob);
    }
    if (ctrl->additional_info && device->string_blob) {
      rec.info_offset = (uint32_t)((const char *)ctrl->additional_info - (const char *)device->string_blob);
    }
    
    fwrite(&rec, sizeof(rec), 1, f);
  }
  
  // Write discrete value records
  for (uint16_t i = 0; i < device->control_count; i++) {
    const midi_control_t *ctrl = &device->controls[i];
    for (uint8_t j = 0; j < ctrl->discrete_count; j++) {
      discrete_value_record_t dv_rec = {0};
      dv_rec.value = ctrl->discrete_values[j].value;
      if (ctrl->discrete_values[j].name && device->string_blob) {
        dv_rec.name_offset = (uint32_t)((const char *)ctrl->discrete_values[j].name -
          (const char *)device->string_blob);
      }
      fwrite(&dv_rec, sizeof(dv_rec), 1, f);
    }
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
  ESP_LOGI(TAG, "Cache generated successfully (%u discrete values)", (unsigned)total_discrete_count);
  return ESP_OK;
}

/**
 * Load device from binary cache
 */
device_def_t *load_device_cache(const char *cache_path, const char *slug) {
  ESP_LOGD(TAG, "Loading cache: %s", cache_path);
  
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
  
  // Read control records first to count discrete values
  size_t control_data_size = header.control_count * sizeof(control_record_t);
  control_record_t *records = malloc_prefer_psram(control_data_size);
  if (!records) {
    ESP_LOGE(TAG, "Failed to allocate control records buffer");
    fclose(f);
    return NULL;
  }
  
  if (fread(records, 1, control_data_size, f) != control_data_size) {
    ESP_LOGE(TAG, "Failed to read control records");
    free(records);
    fclose(f);
    return NULL;
  }
  
  // Count total discrete values
  uint32_t total_discrete_count = 0;
  for (uint32_t i = 0; i < header.control_count; i++) {
    total_discrete_count += records[i].discrete_count;
  }
  
  // Read discrete value records
  size_t discrete_data_size = total_discrete_count * sizeof(discrete_value_record_t);
  discrete_value_record_t *dv_records = NULL;
  if (total_discrete_count > 0) {
    dv_records = malloc_prefer_psram(discrete_data_size);
    if (!dv_records) {
      ESP_LOGW(TAG, "Failed to allocate discrete value records, continuing without them");
      total_discrete_count = 0;
    } else if (fread(dv_records, 1, discrete_data_size, f) != discrete_data_size) {
      ESP_LOGW(TAG, "Failed to read discrete value records");
      free(dv_records);
      dv_records = NULL;
      total_discrete_count = 0;
    }
  }
  
  // Read string blob
  uint8_t *string_blob = malloc_prefer_psram(header.string_blob_size);
  if (!string_blob) {
    ESP_LOGE(TAG, "Failed to allocate string blob");
    free(dv_records);
    free(records);
    fclose(f);
    return NULL;
  }
  
  if (fread(string_blob, 1, header.string_blob_size, f) != header.string_blob_size) {
    ESP_LOGE(TAG, "Failed to read string blob");
    free(string_blob);
    free(dv_records);
    free(records);
    fclose(f);
    return NULL;
  }
  
  fclose(f);
  
  // Allocate device structure
  device_def_t *device = calloc_prefer_psram(1, sizeof(device_def_t));
  if (!device) {
    ESP_LOGE(TAG, "Failed to allocate device_def_t");
    free(string_blob);
    free(dv_records);
    free(records);
    return NULL;
  }
  
  strncpy(device->slug, slug, sizeof(device->slug) - 1);
  device->control_count = header.control_count;
  device->string_blob_size = header.string_blob_size;
  device->string_blob = string_blob;
  
  // Note: Cache doesn't store device metadata (trs_type, midi_channel, etc.)
  // These will be filled in by assets_load_device from the manifest
  
  // Allocate controls array
  device->controls = calloc_prefer_psram(device->control_count, sizeof(midi_control_t));
  if (!device->controls) {
    ESP_LOGE(TAG, "Failed to allocate controls array");
    free(device->string_blob);
    free(device);
    free(dv_records);
    free(records);
    return NULL;
  }
  
  // Allocate discrete values array if needed
  discrete_value_t *all_discrete = NULL;
  discrete_value_t *discrete_ptr = NULL;
  if (total_discrete_count > 0 && dv_records) {
    all_discrete = calloc_prefer_psram(total_discrete_count, sizeof(discrete_value_t));
    discrete_ptr = all_discrete;
    if (!all_discrete) {
      ESP_LOGW(TAG, "Failed to allocate discrete values, continuing without them");
    }
  }
  
  // Parse control records and discrete values
  uint32_t dv_idx = 0;
  for (uint16_t i = 0; i < device->control_count; i++) {
    midi_control_t *ctrl = &device->controls[i];
    control_record_t *rec = &records[i];
    
    ctrl->type = rec->type;
    ctrl->id = rec->id;
    ctrl->min = rec->min;
    ctrl->max = rec->max;
    ctrl->flags = rec->flags;
    ctrl->discrete_count = rec->discrete_count;
    
    // Set name pointer
    if (rec->name_offset < device->string_blob_size)
      ctrl->name = (const char *)device->string_blob + rec->name_offset;
    
    // Set info pointer
    if (rec->info_offset > 0 && rec->info_offset < device->string_blob_size)
      ctrl->additional_info = (const char *)device->string_blob + rec->info_offset;
    
    // Set discrete values
    if (rec->discrete_count > 0 && discrete_ptr && dv_records) {
      ctrl->discrete_values = discrete_ptr;
      for (uint8_t j = 0; j < rec->discrete_count; j++) {
        discrete_value_record_t *dv_rec = &dv_records[dv_idx++];
        discrete_ptr->value = dv_rec->value;
        if (dv_rec->name_offset < device->string_blob_size)
          discrete_ptr->name = (const char *)device->string_blob + dv_rec->name_offset;
        discrete_ptr++;
      }
    }
  }
  
  free(records);
  free(dv_records);
  
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
  
  ESP_LOGD(TAG, "Cache loaded successfully: %u controls, %u discrete values",
    (unsigned)device->control_count, (unsigned)total_discrete_count);
  return device;
}

