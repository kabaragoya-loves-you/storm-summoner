#include "assets_manager.h"
#include "assets_types.h"
#include "memory_utils.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define TAG "assets_manager"

// Forward declarations from other modules
extern device_def_t *assets_parse_device_file(const char *filepath, const char *slug);
extern device_def_t *load_device_cache(const char *cache_path, const char *slug);
extern esp_err_t generate_device_cache(const device_def_t *device, const char *cache_path);

// Global manifest
static manifest_t g_manifest = {0};
static bool g_initialized = false;
static bool g_userdata_available = false;

// Forward decl: vendor cache lives at the bottom of the file but
// assets_manager_reload_manifest() needs to invalidate it.
static bool s_vendors_cached;

bool assets_userdata_available(void) {
  return g_userdata_available;
}

/**
 * Parse a manifest JSON string into the supplied output struct.
 *
 * @param json_str       NUL-terminated JSON
 * @param out            Output manifest. Caller owns out->devices after success.
 * @param from_userdata  true tags every parsed entry as RW-origin so
 *                       assets_load_device() resolves its JSON path under
 *                       USERDATA_BASE_PATH; false tags as RO origin.
 */
static esp_err_t parse_manifest_into(const char *json_str, manifest_t *out, bool from_userdata) {
  if (!out) return ESP_ERR_INVALID_ARG;
  out->schema = 0;
  out->device_count = 0;
  out->devices = NULL;

  cJSON *root = cJSON_Parse(json_str);
  if (!root) {
    ESP_LOGE(TAG, "Failed to parse manifest.json");
    return ESP_FAIL;
  }

  cJSON *schema = cJSON_GetObjectItem(root, "schema");
  if (schema && cJSON_IsNumber(schema)) {
    out->schema = schema->valueint;
  }

  cJSON *devices = cJSON_GetObjectItem(root, "devices");
  if (!devices || !cJSON_IsArray(devices)) {
    ESP_LOGE(TAG, "No devices array in manifest");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  out->device_count = cJSON_GetArraySize(devices);

  if (out->device_count == 0) {
    ESP_LOGW(TAG, "Manifest contains no devices");
    cJSON_Delete(root);
    return ESP_OK;
  }

  out->devices = calloc_prefer_psram(out->device_count, sizeof(manifest_device_t));
  if (!out->devices) {
    ESP_LOGE(TAG, "Failed to allocate devices array");
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  int idx = 0;
  cJSON *dev_item;
  cJSON_ArrayForEach(dev_item, devices) {
    manifest_device_t *dev = &out->devices[idx];
    dev->from_userdata = from_userdata;
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(dev_item, "slug");
    if (item && cJSON_IsString(item))
      strncpy(dev->slug, item->valuestring, sizeof(dev->slug) - 1);
    
    // Try "name" first, then "product" (Ruby outputs product, we store as name)
    item = cJSON_GetObjectItem(dev_item, "name");
    if (!item) item = cJSON_GetObjectItem(dev_item, "product");
    if (item && cJSON_IsString(item))
      strncpy(dev->name, item->valuestring, sizeof(dev->name) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "vendor");
    if (item && cJSON_IsString(item))
      strncpy(dev->vendor, item->valuestring, sizeof(dev->vendor) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "version");
    if (item && cJSON_IsString(item))
      strncpy(dev->version, item->valuestring, sizeof(dev->version) - 1);
    
    // Try "file" first, then "path" (Ruby outputs path, we store as file)
    item = cJSON_GetObjectItem(dev_item, "file");
    if (!item) item = cJSON_GetObjectItem(dev_item, "path");
    if (item && cJSON_IsString(item))
      strncpy(dev->file, item->valuestring, sizeof(dev->file) - 1);
    
    item = cJSON_GetObjectItem(dev_item, "size");
    if (item && cJSON_IsNumber(item))
      dev->size = item->valueint;
    
    // Parse trsType
    item = cJSON_GetObjectItem(dev_item, "trsType");
    if (item && cJSON_IsString(item)) {
      const char *trs = item->valuestring;
      if (strcmp(trs, "TYPE_A") == 0) dev->trs_type = MIDI_TRS_TYPE_A;
      else if (strcmp(trs, "TYPE_B") == 0) dev->trs_type = MIDI_TRS_TYPE_B;
      else if (strcmp(trs, "TYPE_TS") == 0) dev->trs_type = MIDI_TRS_TYPE_TS;
      else if (strcmp(trs, "BOTH") == 0) dev->trs_type = MIDI_TRS_TYPE_BOTH;
      else dev->trs_type = MIDI_TRS_UNKNOWN;
    }
    
    // Parse midiChannel
    item = cJSON_GetObjectItem(dev_item, "midiChannel");
    if (item && cJSON_IsNumber(item)) {
      int ch = item->valueint;
      if (ch >= 1 && ch <= 16) dev->midi_channel = (uint8_t)ch;
    }
    
    // Parse receives array for flags
    cJSON *receives = cJSON_GetObjectItem(dev_item, "receives");
    if (receives && cJSON_IsArray(receives)) {
      cJSON *recv_item;
      cJSON_ArrayForEach(recv_item, receives) {
        if (cJSON_IsString(recv_item)) {
          if (strcmp(recv_item->valuestring, "PROGRAM_CHANGE") == 0)
            dev->receives_pc = true;
          else if (strcmp(recv_item->valuestring, "CLOCK") == 0)
            dev->receives_clock = true;
          else if (strcmp(recv_item->valuestring, "NOTE_ON") == 0 ||
                   strcmp(recv_item->valuestring, "NOTE_OFF") == 0)
            dev->receives_notes = true;
        }
      }
    }
    
    // Parse transmits array for flags
    cJSON *transmits = cJSON_GetObjectItem(dev_item, "transmits");
    if (transmits && cJSON_IsArray(transmits)) {
      cJSON *trans_item;
      cJSON_ArrayForEach(trans_item, transmits) {
        if (cJSON_IsString(trans_item)) {
          if (strcmp(trans_item->valuestring, "PROGRAM_CHANGE") == 0)
            dev->transmits_pc = true;
        }
      }
    }
    
    // Parse x_pc for PC info (needed when loading from cache)
    dev->pc_index_base = 0;
    dev->pc_count = 128;
    dev->pc_bank_mode = PC_BANK_SELECT_NONE;
    cJSON *x_pc = cJSON_GetObjectItem(dev_item, "x_pc");
    if (x_pc && cJSON_IsObject(x_pc)) {
      cJSON *index_base = cJSON_GetObjectItem(x_pc, "indexBase");
      if (index_base && cJSON_IsNumber(index_base))
        dev->pc_index_base = (uint16_t)index_base->valueint;
      
      cJSON *count = cJSON_GetObjectItem(x_pc, "count");
      if (count && cJSON_IsNumber(count))
        dev->pc_count = (uint16_t)count->valueint;
      
      cJSON *bank_mode = cJSON_GetObjectItem(x_pc, "bankSelectMode");
      if (bank_mode && cJSON_IsString(bank_mode)) {
        const char *mode = bank_mode->valuestring;
        if (strcmp(mode, "CC0") == 0) dev->pc_bank_mode = PC_BANK_SELECT_CC0;
        else if (strcmp(mode, "CC0_CC32") == 0) dev->pc_bank_mode = PC_BANK_SELECT_CC0_CC32;
      }
    }
    
    idx++;
  }

  cJSON_Delete(root);

  ESP_LOGI(TAG, "Manifest parsed: schema=%lu, devices=%lu, origin=%s",
    (unsigned long)out->schema, (unsigned long)out->device_count,
    from_userdata ? "userdata" : "assets");

  return ESP_OK;
}

/**
 * Load and parse a manifest file from disk into the supplied output. Returns
 * ESP_FAIL (without populating out) if the file cannot be read; that's the
 * normal first-boot state for the userdata manifest and is not an error from
 * the caller's perspective.
 */
static esp_err_t load_manifest_file(const char *path, manifest_t *out, bool from_userdata) {
  ESP_LOGI(TAG, "Loading manifest: %s", path);

  FILE *f = fopen(path, "rb");
  if (!f) {
    ESP_LOGW(TAG, "manifest not present: %s", path);
    return ESP_FAIL;
  }

  struct stat st;
  if (stat(path, &st) != 0) {
    fclose(f);
    return ESP_FAIL;
  }

  char *json_buf = malloc(st.st_size + 1);
  if (!json_buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t read_bytes = fread(json_buf, 1, st.st_size, f);
  fclose(f);

  if (read_bytes != (size_t)st.st_size) {
    free(json_buf);
    return ESP_FAIL;
  }

  json_buf[st.st_size] = '\0';
  esp_err_t ret = parse_manifest_into(json_buf, out, from_userdata);
  free(json_buf);
  return ret;
}

/**
 * Load shared (RO) and user (RW) device manifests and merge them into
 * g_manifest with overlay semantics: any RW entry whose slug matches an RO
 * entry replaces that RO entry; remaining RW entries are appended.
 *
 * The result is a single deduplicated array, so build_vendor_cache and other
 * callers don't see ghost RO entries that the user has overridden. The
 * `from_userdata` flag on each entry tells assets_load_device() which mount
 * to read the JSON from.
 *
 * Either manifest may be absent. RO absent on a fresh dev board (no /assets
 * content) is logged as a warning. RW absent during a degraded boot or before
 * the user creates any overrides is the normal case.
 */
static esp_err_t load_manifest(void) {
  manifest_t ro = {0};
  manifest_t rw = {0};

  esp_err_t ro_err = load_manifest_file(
    ASSETS_BASE_PATH "/devices/manifest.json", &ro, false);
  if (ro_err != ESP_OK) {
    ESP_LOGW(TAG, "shared device manifest unavailable - shared device list will be empty");
  }

  if (g_userdata_available) {
    esp_err_t rw_err = load_manifest_file(
      USERDATA_BASE_PATH "/devices/manifest.json", &rw, true);
    if (rw_err != ESP_OK) {
      ESP_LOGI(TAG, "user device manifest not present (first boot or no overrides)");
    }
  }

  // Drop any prior global manifest (reload paths).
  if (g_manifest.devices) {
    free(g_manifest.devices);
    g_manifest.devices = NULL;
    g_manifest.device_count = 0;
  }

  size_t cap = ro.device_count + rw.device_count;
  if (cap == 0) {
    if (ro.devices) free(ro.devices);
    if (rw.devices) free(rw.devices);
    return (ro_err != ESP_OK) ? ro_err : ESP_OK;
  }

  g_manifest.devices = calloc_prefer_psram(cap, sizeof(manifest_device_t));
  if (!g_manifest.devices) {
    if (ro.devices) free(ro.devices);
    if (rw.devices) free(rw.devices);
    return ESP_ERR_NO_MEM;
  }
  g_manifest.schema = (rw.schema > 0) ? rw.schema : ro.schema;

  size_t out_idx = 0;
  // RO entries first, skipping any whose slug is overridden in RW.
  for (uint32_t i = 0; i < ro.device_count; i++) {
    bool overridden = false;
    for (uint32_t j = 0; j < rw.device_count; j++) {
      if (strcmp(ro.devices[i].slug, rw.devices[j].slug) == 0) {
        overridden = true;
        break;
      }
    }
    if (overridden) {
      ESP_LOGD(TAG, "RW overrides RO entry %s", ro.devices[i].slug);
      continue;
    }
    g_manifest.devices[out_idx++] = ro.devices[i];
  }
  // Then all RW entries (already tagged from_userdata=true by parse).
  for (uint32_t j = 0; j < rw.device_count; j++) {
    g_manifest.devices[out_idx++] = rw.devices[j];
  }
  g_manifest.device_count = out_idx;

  if (ro.devices) free(ro.devices);
  if (rw.devices) free(rw.devices);

  ESP_LOGI(TAG, "Merged manifest: %lu devices (%lu shared kept, %lu user)",
    (unsigned long)g_manifest.device_count,
    (unsigned long)(out_idx - rw.device_count),
    (unsigned long)rw.device_count);
  return ESP_OK;
}

/**
 * Initialize assets manager
 */
// PSRAM allocation for cJSON (set once globally)
static void *cjson_psram_malloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static void cjson_psram_free(void *ptr) {
  heap_caps_free(ptr);
}

esp_err_t assets_manager_init(void) {
  if (g_initialized) {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing assets manager");
  
  // Configure cJSON to use PSRAM globally
  cJSON_Hooks psram_hooks = {
    .malloc_fn = cjson_psram_malloc,
    .free_fn = cjson_psram_free
  };
  cJSON_InitHooks(&psram_hooks);

  // Mount the RO `assets` partition. Failure here is fatal because the UI
  // images and shared device DB live there.
  esp_vfs_littlefs_conf_t assets_conf = {
    .base_path = ASSETS_BASE_PATH,
    .partition_label = ASSETS_PARTITION,
    .format_if_mount_failed = false,
    .dont_mount = false
  };
  esp_err_t ret = esp_vfs_littlefs_register(&assets_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount assets LittleFS (%s)", esp_err_to_name(ret));
    return ret;
  }
  size_t a_total = 0, a_used = 0;
  if (esp_littlefs_info(ASSETS_PARTITION, &a_total, &a_used) == ESP_OK) {
    ESP_LOGI(TAG, "assets mounted: used=%u of %u bytes",
      (unsigned)a_used, (unsigned)a_total);
  }

  // Mount the RW `userdata` partition. format_if_mount_failed=true so the
  // first-boot empty partition is auto-formatted. If the mount fails outright
  // we log an ESP_LOGE but continue init - all userdata writes will fail
  // gracefully at the fopen/mkdir layer.
  esp_vfs_littlefs_conf_t userdata_conf = {
    .base_path = USERDATA_BASE_PATH,
    .partition_label = USERDATA_PARTITION,
    .format_if_mount_failed = true,
    .dont_mount = false
  };
  esp_err_t udret = esp_vfs_littlefs_register(&userdata_conf);
  if (udret == ESP_OK) {
    g_userdata_available = true;
    size_t u_total = 0, u_used = 0;
    if (esp_littlefs_info(USERDATA_PARTITION, &u_total, &u_used) == ESP_OK) {
      ESP_LOGI(TAG, "userdata mounted: used=%u of %u bytes",
        (unsigned)u_used, (unsigned)u_total);
    }
    // Seed the directory tree so downstream writers (scene.c, device_config.c,
    // assets_cache.c) can fopen() without each having to create their own
    // parent dirs. mkdir() returning EEXIST is the steady-state case and is
    // not an error.
    static const char *seed_dirs[] = {
      USERDATA_BASE_PATH "/scenes",
      USERDATA_BASE_PATH "/devices",
      USERDATA_BASE_PATH "/devices/user",
      USERDATA_BASE_PATH "/cache",
    };
    for (size_t i = 0; i < sizeof(seed_dirs) / sizeof(seed_dirs[0]); i++) {
      struct stat st;
      if (stat(seed_dirs[i], &st) != 0) {
        if (mkdir(seed_dirs[i], 0755) != 0) {
          ESP_LOGW(TAG, "mkdir(%s) failed (errno=%d)", seed_dirs[i], errno);
        } else {
          ESP_LOGI(TAG, "Created %s", seed_dirs[i]);
        }
      }
    }
  } else {
    g_userdata_available = false;
    ESP_LOGE(TAG, "userdata partition unavailable (%s) - "
      "device booting in degraded mode. Recover with `idf.py erase-flash` and reflash.",
      esp_err_to_name(udret));
  }

  // Load manifest from the RO partition.
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
 * Public wrapper to get manifest device by slug
 */
const manifest_device_t *assets_get_manifest_device(const char *slug) {
  if (!slug) return NULL;
  return find_device_in_manifest(slug);
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
  
  ESP_LOGD(TAG, "Loading device: %s", slug);
  
  // The parsed-device cache lives on the RW userdata partition: it can be
  // regenerated from JSON, so wiping it (or having it absent on a degraded
  // boot) is harmless. Moving it off /assets means an ASSETS OTA does not
  // invalidate caches for unchanged shared devices.
  char cache_path[128];
  snprintf(cache_path, sizeof(cache_path), "%s/cache/%s.bin", USERDATA_BASE_PATH, slug);

  device_def_t *device = load_device_cache(cache_path, slug);
  if (device) {
    // Cache doesn't store device metadata - fill in from manifest
    strncpy(device->name, manifest_dev->name, sizeof(device->name) - 1);
    strncpy(device->vendor, manifest_dev->vendor, sizeof(device->vendor) - 1);
    strncpy(device->version, manifest_dev->version, sizeof(device->version) - 1);
    device->trs_type = (midi_trs_type_t)manifest_dev->trs_type;
    device->midi_channel = manifest_dev->midi_channel;
    device->receives_pc = manifest_dev->receives_pc;
    device->receives_clock = manifest_dev->receives_clock;
    device->receives_notes = manifest_dev->receives_notes;
    device->transmits_pc = manifest_dev->transmits_pc;
    
    // Create pc_info from manifest data
    device->pc_info = calloc_prefer_psram(1, sizeof(program_change_info_t));
    if (device->pc_info) {
      device->pc_info->index_base = manifest_dev->pc_index_base;
      device->pc_info->count = manifest_dev->pc_count;
      device->pc_info->bank_mode = (pc_bank_select_mode_t)manifest_dev->pc_bank_mode;
    }
    
    ESP_LOGD(TAG, "Loaded from cache");
    return device;
  }
  
  // Cache miss - parse JSON. Origin partition determines the mount root.
  ESP_LOGI(TAG, "Cache miss, parsing JSON");

  const char *json_root = manifest_dev->from_userdata
    ? USERDATA_BASE_PATH : ASSETS_BASE_PATH;
  char json_path[256];
  snprintf(json_path, sizeof(json_path), "%s/devices/%s", json_root, manifest_dev->file);

  device = assets_parse_device_file(json_path, slug);
  if (!device) {
    ESP_LOGE(TAG, "Failed to parse device JSON");
    return NULL;
  }
  
  // Generate cache for next time. Skipped (with a warning) when userdata is
  // unavailable - parsing-from-JSON every load is the acceptable fallback.
  if (g_userdata_available) {
    ESP_LOGI(TAG, "Generating cache for future use");
    generate_device_cache(device, cache_path);
  } else {
    ESP_LOGW(TAG, "userdata unavailable - skipping cache write for %s", slug);
  }

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

// Forward declarations from assets_file_ops.c
extern esp_err_t assets_regenerate_devices_manifest(void);
extern esp_err_t assets_regenerate_user_devices_manifest(void);

/**
 * Rebuild user (RW) device manifest by scanning /userdata/devices/, then
 * reload the merged manifest into memory. The shared (RO) manifest is shipped
 * as a build artifact and is never regenerated at runtime - use the dev
 * console's `regenerate_shared_devices` for that.
 *
 * This is what device_config.c invokes after `ensure_default_device_exists`
 * writes a new file under /userdata/devices/user/.
 */
esp_err_t assets_rebuild_manifest(void) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (!g_userdata_available) {
    ESP_LOGW(TAG, "userdata unavailable - skipping user manifest rebuild");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Rebuilding user device manifest...");

  esp_err_t ret = assets_regenerate_user_devices_manifest();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to regenerate user devices manifest");
    return ret;
  }

  // Reload merged manifest (RO + new RW) into memory.
  return assets_manager_reload_manifest();
}

esp_err_t assets_manager_reload_manifest(void) {
  if (!g_initialized) {
    ESP_LOGE(TAG, "Assets manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  ESP_LOGI(TAG, "Reloading device manifest");

  // load_manifest() handles freeing the old global manifest internally and
  // reloads both partitions with the overlay merge.
  esp_err_t ret = load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reload manifest");
    return ret;
  }

  // Vendor cache is derived from g_manifest; invalidate so it rebuilds on
  // next access.
  s_vendors_cached = false;

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
  snprintf(cache_path, sizeof(cache_path), "%s/cache/%s.bin", USERDATA_BASE_PATH, slug);
  
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

/**
 * Get CC name from device profile
 */
const char *assets_get_cc_name(const device_def_t *device, uint8_t cc_num) {
  if (!device)
    return NULL;
  
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl || !ctrl->name)
    return "Undefined";
  
  return ctrl->name;
}

/**
 * Get the index of the discrete value that matches or contains the given value
 * For ranges (e.g., 0-24 = "Stretch", 25-50 = "Blur"), finds which range contains the value
 */
int assets_get_discrete_index(const device_def_t *device, uint8_t cc_num, uint16_t value) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl || !ctrl->discrete_values || ctrl->discrete_count == 0)
    return -1;
  
  // Find the discrete value range that contains this value
  // Discrete values are sorted by value; the active one is the highest that doesn't exceed the input
  int best_idx = -1;
  for (int i = 0; i < ctrl->discrete_count; i++) {
    if (ctrl->discrete_values[i].value <= value) {
      best_idx = i;
    } else {
      break;  // Since values are sorted ascending
    }
  }
  
  return best_idx;
}

/**
 * Get discrete value name for a given MIDI value
 */
const char *assets_get_discrete_name(const device_def_t *device, uint8_t cc_num, uint16_t value) {
  int idx = assets_get_discrete_index(device, cc_num, value);
  if (idx < 0)
    return NULL;
  
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl)
    return NULL;
  
  return ctrl->discrete_values[idx].name;
}

/**
 * Snap a value to the nearest discrete value for a CC
 */
uint16_t assets_snap_to_discrete(const device_def_t *device, uint8_t cc_num, uint16_t value) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl || !ctrl->discrete_values || ctrl->discrete_count == 0)
    return value;  // No discrete values, return original
  
  // Find nearest discrete value
  uint16_t nearest = ctrl->discrete_values[0].value;
  uint16_t min_diff = (value > nearest) ? (value - nearest) : (nearest - value);
  
  for (int i = 1; i < ctrl->discrete_count; i++) {
    uint16_t dv = ctrl->discrete_values[i].value;
    uint16_t diff = (value > dv) ? (value - dv) : (dv - value);
    if (diff < min_diff) {
      min_diff = diff;
      nearest = dv;
    }
  }
  
  return nearest;
}

/**
 * Get next discrete value (for cycling through options)
 */
uint16_t assets_get_next_discrete(const device_def_t *device, uint8_t cc_num, uint16_t current) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl || !ctrl->discrete_values || ctrl->discrete_count == 0)
    return current;  // No discrete values
  
  // Find current position
  int idx = assets_get_discrete_index(device, cc_num, current);
  if (idx < 0)
    idx = 0;  // Default to first if not found
  else
    idx = (idx + 1) % ctrl->discrete_count;  // Wrap to start
  
  return ctrl->discrete_values[idx].value;
}

/**
 * Get previous discrete value (for cycling through options)
 */
uint16_t assets_get_prev_discrete(const device_def_t *device, uint8_t cc_num, uint16_t current) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl || !ctrl->discrete_values || ctrl->discrete_count == 0)
    return current;  // No discrete values
  
  // Find current position
  int idx = assets_get_discrete_index(device, cc_num, current);
  if (idx < 0)
    idx = ctrl->discrete_count - 1;  // Default to last if not found
  else
    idx = (idx == 0) ? ctrl->discrete_count - 1 : idx - 1;  // Wrap to end
  
  return ctrl->discrete_values[idx].value;
}

/**
 * Check if value is valid for a CC according to device min/max
 */
bool assets_validate_cc_value(const device_def_t *device, uint8_t cc_num, uint16_t value) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl)
    return true;  // Permissive: allow if not defined
  
  return (value >= ctrl->min && value <= ctrl->max);
}

/**
 * Clamp value to device min/max for a CC
 */
uint16_t assets_clamp_cc_value(const device_def_t *device, uint8_t cc_num, uint16_t value) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  if (!ctrl)
    return value;  // Return original if not defined
  
  if (value < ctrl->min)
    return ctrl->min;
  if (value > ctrl->max)
    return ctrl->max;
  return value;
}

/**
 * Check if a CC has discrete values defined
 */
bool assets_cc_has_discrete_values(const device_def_t *device, uint8_t cc_num) {
  const midi_control_t *ctrl = assets_get_control_by_cc(device, cc_num);
  return (ctrl && ctrl->discrete_values && ctrl->discrete_count > 0);
}

// ============================================================================
// Vendor enumeration functions
// ============================================================================

// Maximum vendors we expect (can adjust if needed)
#define MAX_VENDORS 64

// Cached vendor list (built on first access).
// `s_vendors_cached` is forward-declared near the top so reload paths can
// invalidate it; redefining here would conflict, so just initialize.
static char* s_vendor_list[MAX_VENDORS];
static uint32_t s_vendor_count = 0;

// Comparison function for qsort (case-insensitive alphabetical)
static int vendor_compare(const void* a, const void* b) {
  const char* va = *(const char**)a;
  const char* vb = *(const char**)b;
  return strcasecmp(va, vb);
}

// Build the cached vendor list from manifest
static void build_vendor_cache(void) {
  if (s_vendors_cached) return;
  
  s_vendor_count = 0;
  
  // Iterate through all devices and collect unique vendors
  for (uint32_t i = 0; i < g_manifest.device_count; i++) {
    const char* vendor = g_manifest.devices[i].vendor;
    if (!vendor || vendor[0] == '\0') continue;
    
    // Check if vendor already in list
    bool found = false;
    for (uint32_t j = 0; j < s_vendor_count; j++) {
      if (strcmp(s_vendor_list[j], vendor) == 0) {
        found = true;
        break;
      }
    }
    
    if (!found && s_vendor_count < MAX_VENDORS) {
      s_vendor_list[s_vendor_count++] = g_manifest.devices[i].vendor;
    }
  }
  
  // Sort alphabetically
  if (s_vendor_count > 1) {
    qsort(s_vendor_list, s_vendor_count, sizeof(char*), vendor_compare);
  }
  
  s_vendors_cached = true;
  ESP_LOGI(TAG, "Built vendor cache: %lu unique vendors", (unsigned long)s_vendor_count);
}

/**
 * Get count of unique vendors
 */
uint32_t assets_get_vendor_count(void) {
  build_vendor_cache();
  return s_vendor_count;
}

/**
 * Get vendor name by index (alphabetically sorted)
 */
const char* assets_get_vendor_by_index(uint32_t idx) {
  build_vendor_cache();
  if (idx >= s_vendor_count) return NULL;
  return s_vendor_list[idx];
}

/**
 * Get device count for a specific vendor
 */
uint32_t assets_get_device_count_for_vendor(const char* vendor) {
  if (!vendor) return 0;
  
  uint32_t count = 0;
  for (uint32_t i = 0; i < g_manifest.device_count; i++) {
    if (strcmp(g_manifest.devices[i].vendor, vendor) == 0) {
      count++;
    }
  }
  return count;
}

/**
 * Get device info by vendor and index within that vendor
 */
esp_err_t assets_get_device_for_vendor(const char* vendor, uint32_t idx,
  const char** slug, const char** name) {
  if (!vendor) return ESP_ERR_INVALID_ARG;
  
  uint32_t current = 0;
  for (uint32_t i = 0; i < g_manifest.device_count; i++) {
    if (strcmp(g_manifest.devices[i].vendor, vendor) == 0) {
      if (current == idx) {
        if (slug) *slug = g_manifest.devices[i].slug;
        if (name) *name = g_manifest.devices[i].name;
        return ESP_OK;
      }
      current++;
    }
  }
  
  return ESP_ERR_NOT_FOUND;
}

