#include "assets_manager.h"
#include "assets_types.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG "assets_manager"
#define ASSETS_BASE_PATH "/assets"
#define ASSETS_PARTITION "assets"

// Forward declarations from other modules
extern device_def_t *assets_parse_device_file(const char *filepath, const char *slug);
extern device_def_t *load_device_cache(const char *cache_path, const char *slug);
extern esp_err_t generate_device_cache(const device_def_t *device, const char *cache_path);

// Global manifest
static manifest_t g_manifest = {0};
static bool g_initialized = false;

/**
 * Parse manifest.json
 */
static esp_err_t parse_manifest(const char *json_str) {
  cJSON *root = cJSON_Parse(json_str);
  if (!root) {
    ESP_LOGE(TAG, "Failed to parse manifest.json");
    return ESP_FAIL;
  }
  
  // Get schema version
  cJSON *schema = cJSON_GetObjectItem(root, "schema");
  if (schema && cJSON_IsNumber(schema)) {
    g_manifest.schema = schema->valueint;
  }
  
  // Parse devices array
  cJSON *devices = cJSON_GetObjectItem(root, "devices");
  if (!devices || !cJSON_IsArray(devices)) {
    ESP_LOGE(TAG, "No devices array in manifest");
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  g_manifest.device_count = cJSON_GetArraySize(devices);
  
  if (g_manifest.device_count == 0) {
    ESP_LOGW(TAG, "Manifest contains no devices");
    cJSON_Delete(root);
    return ESP_OK;
  }
  
  // Allocate devices array in regular heap (manifest is small)
  g_manifest.devices = calloc(g_manifest.device_count, sizeof(manifest_device_t));
  if (!g_manifest.devices) {
    ESP_LOGE(TAG, "Failed to allocate devices array");
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  
  // Parse each device entry
  int idx = 0;
  cJSON *dev_item;
  cJSON_ArrayForEach(dev_item, devices) {
    manifest_device_t *dev = &g_manifest.devices[idx];
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(dev_item, "slug");
    if (item && cJSON_IsString(item))
      strncpy(dev->slug, item->valuestring, sizeof(dev->slug) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "name");
    if (item && cJSON_IsString(item))
      strncpy(dev->name, item->valuestring, sizeof(dev->name) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "vendor");
    if (item && cJSON_IsString(item))
      strncpy(dev->vendor, item->valuestring, sizeof(dev->vendor) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "version");
    if (item && cJSON_IsString(item))
      strncpy(dev->version, item->valuestring, sizeof(dev->version) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "file");
    if (item && cJSON_IsString(item))
      strncpy(dev->file, item->valuestring, sizeof(dev->file) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "size");
    if (item && cJSON_IsNumber(item))
      dev->size = item->valueint;
    
    idx++;
  }
  
  cJSON_Delete(root);
  
  ESP_LOGI(TAG, "Manifest loaded: schema=%lu, devices=%lu", 
    (unsigned long)g_manifest.schema, (unsigned long)g_manifest.device_count);
  
  return ESP_OK;
}

/**
 * Load manifest.json from filesystem
 */
static esp_err_t load_manifest(void) {
  char path[128];
  snprintf(path, sizeof(path), "%s/devices/manifest.json", ASSETS_BASE_PATH);
  
  ESP_LOGI(TAG, "Loading manifest: %s", path);
  
  // Open file
  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open manifest.json");
    return ESP_FAIL;
  }
  
  // Get file size
  struct stat st;
  if (stat(path, &st) != 0) {
    ESP_LOGE(TAG, "Failed to stat manifest.json");
    fclose(f);
    return ESP_FAIL;
  }
  
  // Allocate buffer
  char *json_buf = malloc(st.st_size + 1);
  if (!json_buf) {
    ESP_LOGE(TAG, "Failed to allocate buffer for manifest");
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  
  // Read file
  size_t read_bytes = fread(json_buf, 1, st.st_size, f);
  fclose(f);
  
  if (read_bytes != (size_t)st.st_size) {
    ESP_LOGE(TAG, "Failed to read manifest.json");
    free(json_buf);
    return ESP_FAIL;
  }
  
  json_buf[st.st_size] = '\0';
  
  // Parse manifest
  esp_err_t ret = parse_manifest(json_buf);
  free(json_buf);
  
  return ret;
}

/**
 * Initialize assets manager
 */
esp_err_t assets_manager_init(void) {
  if (g_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing assets manager");
  
  // Configure LittleFS
  esp_vfs_littlefs_conf_t conf = {
    .base_path = ASSETS_BASE_PATH,
    .partition_label = ASSETS_PARTITION,
    .format_if_mount_failed = false,
    .dont_mount = false
  };
  
  // Mount filesystem
  esp_err_t ret = esp_vfs_littlefs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(ret));
    return ret;
  }
  
  // Get filesystem info
  size_t total = 0, used = 0;
  ret = esp_littlefs_info(conf.partition_label, &total, &used);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS mounted: used=%u of %u bytes", (unsigned)used, (unsigned)total);
  }
  
  // Load manifest
  ret = load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load manifest");
    return ret;
  }
  
  g_initialized = true;
  return ESP_OK;
}

/**
 * Get device count
 */
uint32_t assets_get_device_count(void) {
  return g_manifest.device_count;
}

/**
 * Get device info by index
 */
esp_err_t assets_get_device_info(uint32_t idx, const char **slug, const char **name, const char **vendor) {
  if (idx >= g_manifest.device_count)
    return ESP_ERR_INVALID_ARG;
  
  manifest_device_t *dev = &g_manifest.devices[idx];
  
  if (slug) *slug = dev->slug;
  if (name) *name = dev->name;
  if (vendor) *vendor = dev->vendor;
  
  return ESP_OK;
}

/**
 * Find device in manifest by slug
 */
static manifest_device_t *find_device_in_manifest(const char *slug) {
  for (uint32_t i = 0; i < g_manifest.device_count; i++) {
    if (strcmp(g_manifest.devices[i].slug, slug) == 0)
      return &g_manifest.devices[i];
  }
  return NULL;
}

/**
 * Load device by slug
 */
device_def_t *assets_load_device(const char *slug) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return NULL;
  }
  
  // Find device in manifest
  manifest_device_t *manifest_dev = find_device_in_manifest(slug);
  if (!manifest_dev) {
    ESP_LOGE(TAG, "Device not found in manifest: %s", slug);
    return NULL;
  }
  
  ESP_LOGI(TAG, "Loading device: %s", slug);
  
  // Try to load from cache first
  char cache_path[128];
  snprintf(cache_path, sizeof(cache_path), "%s/cache/%s.bin", ASSETS_BASE_PATH, slug);
  
  device_def_t *device = load_device_cache(cache_path, slug);
  if (device) {
    ESP_LOGI(TAG, "Loaded from cache");
    return device;
  }
  
  // Cache miss - parse JSON
  ESP_LOGI(TAG, "Cache miss, parsing JSON");
  
  char json_path[256];
  snprintf(json_path, sizeof(json_path), "%s/devices/%s", ASSETS_BASE_PATH, manifest_dev->file);
  
  device = assets_parse_device_file(json_path, slug);
  if (!device) {
    ESP_LOGE(TAG, "Failed to parse device JSON");
    return NULL;
  }
  
  // Generate cache for next time
  ESP_LOGI(TAG, "Generating cache for future use");
  generate_device_cache(device, cache_path);
  
  return device;
}

/**
 * Free device definition
 */
void assets_free_device(device_def_t *device) {
  if (!device)
    return;
  
  if (device->controls)
    heap_caps_free(device->controls);
  
  if (device->string_blob)
    heap_caps_free(device->string_blob);
  
  if (device->cc_lookup)
    heap_caps_free(device->cc_lookup);
  
  if (device->pc_info) {
    if (device->pc_info->names)
      heap_caps_free((void *)device->pc_info->names);
    heap_caps_free(device->pc_info);
  }
  
  heap_caps_free(device);
}

esp_err_t assets_manager_reload_manifest(void) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Reloading device manifest");

  // Free existing manifest
  if (g_manifest.devices) {
    free(g_manifest.devices);
    g_manifest.devices = NULL;
    g_manifest.device_count = 0;
  }

  // Reload from filesystem
  esp_err_t ret = load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reload manifest");
    return ret;
  }

  ESP_LOGI(TAG, "Manifest reloaded successfully (%u devices)", (unsigned)g_manifest.device_count);
  return ESP_OK;
}

esp_err_t assets_manager_reload_device(const char *slug) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!slug) {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_LOGI(TAG, "Reloading device: %s", slug);

  // Find device in manifest
  manifest_device_t *manifest_dev = find_device_in_manifest(slug);
  if (!manifest_dev) {
    ESP_LOGE(TAG, "Device not found in manifest: %s", slug);
    return ESP_ERR_NOT_FOUND;
  }

  // Delete cache file to force reload from JSON
  char cache_path[128];
  snprintf(cache_path, sizeof(cache_path), "%s/cache/%s.bin", ASSETS_BASE_PATH, slug);
  
  struct stat st;
  if (stat(cache_path, &st) == 0) {
    if (unlink(cache_path) == 0) {
      ESP_LOGI(TAG, "Deleted cache for %s", slug);
    } else {
      ESP_LOGW(TAG, "Failed to delete cache for %s", slug);
    }
  }

  ESP_LOGI(TAG, "Device %s will be reloaded from JSON on next access", slug);
  return ESP_OK;
}

esp_err_t assets_manager_sync_to_msc(void) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Syncing assets to MSC volume");
  
  // This would copy all device profiles and scene files to the MSC RAM volume
  // For now, this is a placeholder as it requires FAT filesystem write support
  // which is complex to implement from scratch
  
  ESP_LOGI(TAG, "MSC sync placeholder - requires FAT write library");
  return ESP_OK;
}

/**
 * Get control by CC number
 */
const midi_control_t *assets_get_control_by_cc(const device_def_t *device, uint8_t cc_num) {
  if (!device || !device->cc_lookup || cc_num >= 128)
    return NULL;
  
  int16_t idx = device->cc_lookup[cc_num];
  if (idx < 0)
    return NULL;
  
  return &device->controls[idx];
}

/**
 * Get control by index
 */
const midi_control_t *assets_get_control_by_index(const device_def_t *device, uint16_t idx) {
  if (!device || idx >= device->control_count)
    return NULL;
  
  return &device->controls[idx];
}

/**
 * Get PC info
 */
const program_change_info_t *assets_get_pc_info(const device_def_t *device) {
  if (!device)
    return NULL;
  
  return device->pc_info;
}

/**
 * Get MIDI TRS type
 */
midi_trs_type_t assets_get_trs_type(const device_def_t *device) {
  if (!device)
    return MIDI_TRS_UNKNOWN;
  
  return device->trs_type;
}

