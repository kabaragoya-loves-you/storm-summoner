#include "assets_file_ops.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "mbedtls/sha256.h"

// Suppress warnings from miniz header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "miniz/miniz.h"
#pragma GCC diagnostic pop

#define TAG "assets_file_ops"

// PSRAM allocation helpers
static void *psram_malloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

static void psram_free(void *ptr) {
  heap_caps_free(ptr);
}
#define ASSETS_BASE_PATH "/assets"
#define MAX_PATH_LEN 320  // Must accommodate base path + d_name (255) + separators

// ============================================================================
// Path helpers
// ============================================================================

bool assets_is_valid_path(const char *path) {
  if (!path) return false;
  return strncmp(path, ASSETS_BASE_PATH, strlen(ASSETS_BASE_PATH)) == 0;
}

const char *assets_get_folder_type(const char *path) {
  if (!path) return NULL;
  
  // Check for each managed folder
  if (strstr(path, "/assets/scenes/") == path || 
      strcmp(path, "/assets/scenes") == 0) {
    return "scenes";
  }
  if (strstr(path, "/assets/devices/") == path || 
      strcmp(path, "/assets/devices") == 0) {
    return "devices";
  }
  if (strstr(path, "/assets/images/") == path || 
      strcmp(path, "/assets/images") == 0) {
    return "images";
  }
  
  return NULL;
}

// ============================================================================
// Manifest regeneration
// ============================================================================

esp_err_t assets_regenerate_scenes_manifest(void) {
  const char *scenes_dir = ASSETS_BASE_PATH "/scenes";
  const char *manifest_path = ASSETS_BASE_PATH "/scenes/manifest.json";
  
  ESP_LOGI(TAG, "Regenerating scenes manifest");
  
  DIR *dir = opendir(scenes_dir);
  if (!dir) {
    ESP_LOGW(TAG, "Cannot open scenes directory");
    return ESP_ERR_NOT_FOUND;
  }
  
  // Create JSON structure
  cJSON *root = cJSON_CreateObject();
  cJSON *scenes = cJSON_CreateArray();
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Match scene_XXX.json pattern
    if (strncmp(entry->d_name, "scene_", 6) != 0) continue;
    char *ext = strstr(entry->d_name, ".json");
    if (!ext || strcmp(ext, ".json") != 0) continue;
    
    // Extract index from filename (scene_001.json -> 0)
    int file_num = atoi(entry->d_name + 6);
    if (file_num < 1) continue;
    int index = file_num - 1;
    
    // Read scene file to get name
    char scene_path[MAX_PATH_LEN];
    snprintf(scene_path, sizeof(scene_path), "%s/%s", scenes_dir, entry->d_name);
    
    FILE *f = fopen(scene_path, "r");
    if (!f) continue;
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize > 16384) {  // Sanity check
      fclose(f);
      continue;
    }
    
    char *json_buf = psram_malloc(fsize + 1);
    if (!json_buf) {
      fclose(f);
      continue;
    }
    
    fread(json_buf, 1, fsize, f);
    fclose(f);
    json_buf[fsize] = '\0';
    
    // Parse to get name
    cJSON *scene_json = cJSON_Parse(json_buf);
    psram_free(json_buf);
    
    char name[64] = "";
    if (scene_json) {
      cJSON *name_item = cJSON_GetObjectItem(scene_json, "name");
      if (name_item && cJSON_IsString(name_item)) {
        strncpy(name, name_item->valuestring, sizeof(name) - 1);
      }
      cJSON_Delete(scene_json);
    }
    
    if (name[0] == '\0') {
      snprintf(name, sizeof(name), "Scene %d", index + 1);
    }
    
    // Add to manifest
    cJSON *entry_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry_obj, "index", index);
    cJSON_AddStringToObject(entry_obj, "name", name);
    cJSON_AddStringToObject(entry_obj, "filename", entry->d_name);
    cJSON_AddItemToArray(scenes, entry_obj);
  }
  
  closedir(dir);
  
  cJSON_AddItemToObject(root, "scenes", scenes);
  
  // Write manifest
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  
  if (!json_str) {
    ESP_LOGE(TAG, "Failed to serialize scenes manifest");
    return ESP_ERR_NO_MEM;
  }
  
  FILE *f = fopen(manifest_path, "w");
  if (!f) {
    free(json_str);
    ESP_LOGE(TAG, "Failed to open manifest for writing");
    return ESP_FAIL;
  }
  
  fputs(json_str, f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Scenes manifest updated");
  return ESP_OK;
}

// Helper: Compute full SHA256 hex string for a file (matches Ruby build_manifest.rb)
// PSRAM-based to avoid stack overflow
static bool compute_file_sha256(const char *path, char *hex_out, size_t hex_size) {
  if (hex_size < 65) return false;  // Need 64 hex chars + null
  
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  
  // Allocate mbedtls context and buffer in PSRAM
  mbedtls_sha256_context *ctx = psram_malloc(sizeof(mbedtls_sha256_context));
  uint8_t *buf = psram_malloc(1024);  // Larger buffer is fine in PSRAM
  uint8_t *hash = psram_malloc(32);
  
  if (!ctx || !buf || !hash) {
    fclose(f);
    psram_free(ctx);
    psram_free(buf);
    psram_free(hash);
    return false;
  }
  
  mbedtls_sha256_init(ctx);
  mbedtls_sha256_starts(ctx, 0);  // 0 = SHA256 (not SHA224)
  
  size_t bytes_read;
  while ((bytes_read = fread(buf, 1, 1024, f)) > 0) {
    mbedtls_sha256_update(ctx, buf, bytes_read);
  }
  fclose(f);
  
  mbedtls_sha256_finish(ctx, hash);
  mbedtls_sha256_free(ctx);
  
  // Convert to full 64-char hex string (matches Ruby hexdigest)
  for (int i = 0; i < 32; i++) {
    snprintf(hex_out + (i * 2), 3, "%02x", hash[i]);
  }
  hex_out[64] = '\0';
  
  psram_free(ctx);
  psram_free(buf);
  psram_free(hash);
  return true;
}

// Helper: Add device JSON file to manifest array (MIDI-RTC Schema format)
// Uses heap allocation to avoid stack overflow in console task
static void add_device_to_manifest(const char *device_path, const char *vendor_dir, 
                                   const char *filename, cJSON *devices, int *count) {
  struct stat st;
  if (stat(device_path, &st) != 0) return;
  
  FILE *f = fopen(device_path, "r");
  if (!f) return;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  if (fsize > 65536) {
    fclose(f);
    return;
  }
  
  char *json_buf = psram_malloc(fsize + 1);
  if (!json_buf) {
    fclose(f);
    return;
  }
  
  fread(json_buf, 1, fsize, f);
  fclose(f);
  json_buf[fsize] = '\0';
  
  cJSON *device_json = cJSON_Parse(json_buf);
  psram_free(json_buf);
  
  if (!device_json) return;
  
  // Allocate string buffers in PSRAM
  char *product = psram_malloc(128);
  char *slug = psram_malloc(256);
  char *vendor_display = psram_malloc(128);
  char *product_display = psram_malloc(128);
  char *path = psram_malloc(MAX_PATH_LEN);
  char *sha256 = psram_malloc(65);  // 64 hex chars + null (full SHA256)
  
  if (!product || !slug || !vendor_display || !product_display || !path || !sha256) {
    cJSON_Delete(device_json);
    psram_free(product); psram_free(slug); psram_free(vendor_display);
    psram_free(product_display); psram_free(path); psram_free(sha256);
    return;
  }
  
  // Extract product name from filename (without .json extension)
  strncpy(product, filename, 127);
  product[127] = '\0';
  char *dot = strrchr(product, '.');
  if (dot) *dot = '\0';
  
  // Get implementationVersion from JSON
  cJSON *version_item = cJSON_GetObjectItem(device_json, "implementationVersion");
  const char *version = (version_item && cJSON_IsString(version_item)) ? version_item->valuestring : "0";
  
  // Build slug: vendor.product@version
  snprintf(slug, 256, "%s.%s@%s", vendor_dir, product, version);
  
  // Keep vendor and product matching the slug format (underscores)
  strncpy(vendor_display, vendor_dir, 127);
  vendor_display[127] = '\0';
  strncpy(product_display, product, 127);
  product_display[127] = '\0';
  
  // Build path: devices/vendor/product.json
  snprintf(path, MAX_PATH_LEN, "devices/%s/%s", vendor_dir, filename);
  
  // Compute full SHA256 (matches Ruby build_manifest.rb)
  if (!compute_file_sha256(device_path, sha256, 65)) {
    sha256[0] = '\0';
  }
  
  // Get receives and transmits arrays
  cJSON *receives = cJSON_GetObjectItem(device_json, "receives");
  cJSON *transmits = cJSON_GetObjectItem(device_json, "transmits");
  
  // Count CC and NRPN commands
  cJSON *cc_commands = cJSON_GetObjectItem(device_json, "controlChangeCommands");
  cJSON *nrpn_commands = cJSON_GetObjectItem(device_json, "nrpnCommands");
  int cc_count = cJSON_IsArray(cc_commands) ? cJSON_GetArraySize(cc_commands) : 0;
  int nrpn_count = cJSON_IsArray(nrpn_commands) ? cJSON_GetArraySize(nrpn_commands) : 0;
  
  // Get x_pc if present
  cJSON *x_pc = cJSON_GetObjectItem(device_json, "x_pc");
  
  // Create manifest entry
  cJSON *entry_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(entry_obj, "slug", slug);
  cJSON_AddStringToObject(entry_obj, "vendor", vendor_display);
  cJSON_AddStringToObject(entry_obj, "product", product_display);
  cJSON_AddStringToObject(entry_obj, "version", version);
  cJSON_AddStringToObject(entry_obj, "path", path);
  if (sha256[0]) {
    cJSON_AddStringToObject(entry_obj, "sha256", sha256);
  }
  cJSON_AddNumberToObject(entry_obj, "size", (double)st.st_size);
  
  // Add receives array (copy items)
  cJSON *receives_copy = cJSON_CreateArray();
  if (receives && cJSON_IsArray(receives)) {
    cJSON *item;
    cJSON_ArrayForEach(item, receives) {
      if (cJSON_IsString(item)) {
        cJSON_AddItemToArray(receives_copy, cJSON_CreateString(item->valuestring));
      }
    }
  }
  cJSON_AddItemToObject(entry_obj, "receives", receives_copy);
  
  // Add transmits array (copy items)
  cJSON *transmits_copy = cJSON_CreateArray();
  if (transmits && cJSON_IsArray(transmits)) {
    cJSON *item;
    cJSON_ArrayForEach(item, transmits) {
      if (cJSON_IsString(item)) {
        cJSON_AddItemToArray(transmits_copy, cJSON_CreateString(item->valuestring));
      }
    }
  }
  cJSON_AddItemToObject(entry_obj, "transmits", transmits_copy);
  
  cJSON_AddNumberToObject(entry_obj, "ccCount", cc_count);
  cJSON_AddNumberToObject(entry_obj, "nrpnCount", nrpn_count);
  
  // Add x_pc if it's an object (deep copy)
  if (x_pc && cJSON_IsObject(x_pc)) {
    cJSON *x_pc_copy = cJSON_CreateObject();
    cJSON *x_pc_item;
    cJSON_ArrayForEach(x_pc_item, x_pc) {
      if (cJSON_IsNumber(x_pc_item)) {
        cJSON_AddNumberToObject(x_pc_copy, x_pc_item->string, x_pc_item->valuedouble);
      } else if (cJSON_IsString(x_pc_item)) {
        cJSON_AddStringToObject(x_pc_copy, x_pc_item->string, x_pc_item->valuestring);
      }
    }
    cJSON_AddItemToObject(entry_obj, "x_pc", x_pc_copy);
  }
  
  cJSON_AddItemToArray(devices, entry_obj);
  cJSON_Delete(device_json);
  (*count)++;
  
  // Free PSRAM-allocated strings
  psram_free(product);
  psram_free(slug);
  psram_free(vendor_display);
  psram_free(product_display);
  psram_free(path);
  psram_free(sha256);
}

// Helper: Recursively scan directory for device JSON files
// Uses PSRAM allocation for path buffer to reduce stack usage in recursion
static void scan_devices_dir(const char *base_dir, const char *vendor_name, cJSON *devices, int *count) {
  DIR *dir = opendir(base_dir);
  if (!dir) return;
  
  char *full_path = psram_malloc(MAX_PATH_LEN);
  if (!full_path) {
    closedir(dir);
    return;
  }
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;  // Skip hidden files
    if (strcmp(entry->d_name, "manifest.json") == 0) continue;
    if (strcmp(entry->d_name, "LICENSE") == 0) continue;
    if (strcmp(entry->d_name, "README.md") == 0) continue;
    if (strcmp(entry->d_name, "cache") == 0) continue;  // Skip cache directory
    if (strcmp(entry->d_name, "tools") == 0) continue;  // Skip tools directory
    
    snprintf(full_path, MAX_PATH_LEN, "%s/%s", base_dir, entry->d_name);
    
    struct stat st;
    if (stat(full_path, &st) != 0) continue;
    
    if (S_ISDIR(st.st_mode)) {
      // This is a vendor directory - recurse with vendor name
      scan_devices_dir(full_path, entry->d_name, devices, count);
    } else if (vendor_name[0] != '\0') {
      // We're inside a vendor directory - check if it's a JSON file
      char *ext = strstr(entry->d_name, ".json");
      if (ext && strcmp(ext, ".json") == 0) {
        add_device_to_manifest(full_path, vendor_name, entry->d_name, devices, count);
      }
    }
  }
  
  psram_free(full_path);
  closedir(dir);
}

esp_err_t assets_regenerate_devices_manifest(void) {
  // The midi-devices repo structure: /devices/devices/<vendor>/<device>.json
  const char *devices_dir = ASSETS_BASE_PATH "/devices/devices";
  const char *manifest_path = ASSETS_BASE_PATH "/devices/manifest.json";
  
  ESP_LOGI(TAG, "Regenerating devices manifest");
  
  // Check if nested devices folder exists
  struct stat st;
  if (stat(devices_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
    // Fall back to checking directly in /devices/
    devices_dir = ASSETS_BASE_PATH "/devices";
    if (stat(devices_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
      ESP_LOGW(TAG, "Cannot find devices directory");
      return ESP_ERR_NOT_FOUND;
    }
  }
  
  // Get current time for generatedAt
  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
  
  // Create JSON structure
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "schema", 1);
  cJSON_AddStringToObject(root, "generatedAt", timestamp);
  cJSON *devices = cJSON_CreateArray();
  int count = 0;
  
  // Recursively scan for device files (start with empty vendor name)
  scan_devices_dir(devices_dir, "", devices, &count);
  
  cJSON_AddNumberToObject(root, "count", count);
  cJSON_AddItemToObject(root, "devices", devices);
  
  // Write manifest
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  
  if (!json_str) {
    ESP_LOGE(TAG, "Failed to serialize devices manifest");
    return ESP_ERR_NO_MEM;
  }
  
  FILE *f = fopen(manifest_path, "w");
  if (!f) {
    free(json_str);
    ESP_LOGE(TAG, "Failed to open manifest for writing");
    return ESP_FAIL;
  }
  
  fputs(json_str, f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Devices manifest updated (%d devices)", count);
  return ESP_OK;
}

esp_err_t assets_regenerate_images_manifest(void) {
  const char *images_dir = ASSETS_BASE_PATH "/images";
  const char *manifest_path = ASSETS_BASE_PATH "/images/manifest.json";
  
  ESP_LOGI(TAG, "Regenerating images manifest");
  
  // Check if images directory exists
  struct stat st;
  if (stat(images_dir, &st) != 0) {
    ESP_LOGW(TAG, "Images directory does not exist");
    return ESP_ERR_NOT_FOUND;
  }
  
  DIR *dir = opendir(images_dir);
  if (!dir) {
    ESP_LOGW(TAG, "Cannot open images directory");
    return ESP_ERR_NOT_FOUND;
  }
  
  // Create JSON structure
  cJSON *root = cJSON_CreateObject();
  cJSON *images = cJSON_CreateArray();
  int count = 0;
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip manifest and non-bin files
    if (strcmp(entry->d_name, "manifest.json") == 0) continue;
    
    // Match *.bin or *.bin.z patterns
    bool is_compressed = false;
    char *ext = strstr(entry->d_name, ".bin.z");
    if (ext && strcmp(ext, ".bin.z") == 0) {
      is_compressed = true;
    } else {
      ext = strstr(entry->d_name, ".bin");
      if (!ext || strcmp(ext, ".bin") != 0) continue;
    }
    
    // Skip if there's a corresponding uncompressed version (we prefer .bin.z)
    if (!is_compressed) {
      char compressed_name[MAX_PATH_LEN];
      snprintf(compressed_name, sizeof(compressed_name), "%s/%s.z", images_dir, entry->d_name);
      if (stat(compressed_name, &st) == 0) continue;  // Skip, .z version exists
    }
    
    // Build full path
    char image_path[MAX_PATH_LEN];
    snprintf(image_path, sizeof(image_path), "%s/%s", images_dir, entry->d_name);
    
    if (stat(image_path, &st) != 0) continue;
    
    // Generate display name from filename
    char name[64];
    strncpy(name, entry->d_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    
    // Remove extension(s)
    char *dot = strstr(name, ".bin");
    if (dot) *dot = '\0';
    
    // Replace underscores with spaces for display
    for (char *p = name; *p; p++) {
      if (*p == '_') *p = ' ';
    }
    
    // Capitalize first letter
    if (name[0] >= 'a' && name[0] <= 'z') {
      name[0] -= 32;
    }
    
    // Build manifest entry
    cJSON *entry_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(entry_obj, "filename", entry->d_name);
    cJSON_AddStringToObject(entry_obj, "name", name);
    cJSON_AddNumberToObject(entry_obj, "size", (double)st.st_size);
    cJSON_AddBoolToObject(entry_obj, "compressed", is_compressed);
    
    cJSON_AddItemToArray(images, entry_obj);
    count++;
  }
  
  closedir(dir);
  
  cJSON_AddNumberToObject(root, "count", count);
  cJSON_AddItemToObject(root, "images", images);
  
  // Write manifest
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  
  if (!json_str) {
    ESP_LOGE(TAG, "Failed to serialize images manifest");
    return ESP_ERR_NO_MEM;
  }
  
  FILE *f = fopen(manifest_path, "w");
  if (!f) {
    free(json_str);
    // If directory exists but can't write, log but don't fail
    ESP_LOGW(TAG, "Failed to write images manifest");
    return ESP_FAIL;
  }
  
  fputs(json_str, f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Images manifest updated (%d images)", count);
  return ESP_OK;
}

// ============================================================================
// Recursive delete
// ============================================================================

esp_err_t assets_recursive_delete(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    ESP_LOGW(TAG, "Path not found: %s", path);
    return ESP_ERR_NOT_FOUND;
  }
  
  // If it's a file, just delete it
  if (!S_ISDIR(st.st_mode)) {
    if (unlink(path) == 0) {
      return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to delete file: %s", path);
    return ESP_FAIL;
  }
  
  // It's a directory - recurse
  DIR *dir = opendir(path);
  if (!dir) {
    ESP_LOGE(TAG, "Cannot open directory: %s", path);
    return ESP_FAIL;
  }
  
  char *child_path = psram_malloc(MAX_PATH_LEN);
  if (!child_path) {
    closedir(dir);
    return ESP_ERR_NO_MEM;
  }
  
  esp_err_t result = ESP_OK;
  struct dirent *entry;
  
  while ((entry = readdir(dir)) != NULL) {
    // Skip . and ..
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    
    snprintf(child_path, MAX_PATH_LEN, "%s/%s", path, entry->d_name);
    
    // Recursively delete
    esp_err_t child_result = assets_recursive_delete(child_path);
    if (child_result != ESP_OK) {
      result = child_result;
      // Continue trying to delete other entries
    }
  }
  
  psram_free(child_path);
  closedir(dir);
  
  // Now remove the empty directory
  if (rmdir(path) != 0) {
    ESP_LOGE(TAG, "Failed to remove directory: %s", path);
    return ESP_FAIL;
  }
  
  return result;
}

// ============================================================================
// ZIP extraction (from PSRAM buffer to filesystem)
// ============================================================================

esp_err_t assets_extract_zip(const uint8_t *zip_data, size_t zip_size, const char *dest_path) {
  ESP_LOGI(TAG, "Extracting ZIP (%zu bytes) to %s", zip_size, dest_path);
  
  mz_zip_archive *zip = psram_malloc(sizeof(mz_zip_archive));
  if (!zip) {
    ESP_LOGE(TAG, "Failed to allocate ZIP archive struct");
    return ESP_ERR_NO_MEM;
  }
  
  memset(zip, 0, sizeof(mz_zip_archive));
  
  // Initialize from memory buffer
  if (!mz_zip_reader_init_mem(zip, zip_data, zip_size, 0)) {
    ESP_LOGE(TAG, "Failed to open ZIP archive");
    psram_free(zip);
    return ESP_ERR_INVALID_ARG;
  }
  
  int num_files = (int)mz_zip_reader_get_num_files(zip);
  ESP_LOGI(TAG, "ZIP contains %d entries", num_files);
  
  char *file_path = psram_malloc(MAX_PATH_LEN);
  char *full_path = psram_malloc(MAX_PATH_LEN);
  uint8_t *file_buf = NULL;
  
  if (!file_path || !full_path) {
    mz_zip_reader_end(zip);
    psram_free(zip);
    psram_free(file_path);
    psram_free(full_path);
    return ESP_ERR_NO_MEM;
  }
  
  esp_err_t result = ESP_OK;
  
  for (int i = 0; i < num_files; i++) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(zip, i, &file_stat)) {
      ESP_LOGW(TAG, "Failed to stat ZIP entry %d", i);
      continue;
    }
    
    // Check path length before building full path
    size_t dest_len = strlen(dest_path);
    size_t name_len = strlen(file_stat.m_filename);
    if (dest_len + 1 + name_len >= MAX_PATH_LEN) {
      ESP_LOGW(TAG, "Path too long, skipping: %s", file_stat.m_filename);
      continue;
    }
    
    // Build destination path (length already validated above)
    // Copy in two steps to avoid format-truncation warning
    strcpy(full_path, dest_path);
    strcat(full_path, "/");
    strcat(full_path, file_stat.m_filename);
    
    // Check if it's a directory
    if (mz_zip_reader_is_file_a_directory(zip, i)) {
      // Create directory
      mkdir(full_path, 0755);
      ESP_LOGD(TAG, "Created directory: %s", full_path);
      continue;
    }
    
    // It's a file - ensure parent directory exists
    strncpy(file_path, full_path, MAX_PATH_LEN - 1);
    file_path[MAX_PATH_LEN - 1] = '\0';
    char *last_slash = strrchr(file_path, '/');
    if (last_slash) {
      *last_slash = '\0';
      // Create parent directories recursively
      char *p = file_path;
      while (*p) {
        if (*p == '/') {
          *p = '\0';
          mkdir(file_path, 0755);
          *p = '/';
        }
        p++;
      }
      mkdir(file_path, 0755);
    }
    
    // Extract file to PSRAM buffer first
    size_t file_size = (size_t)file_stat.m_uncomp_size;
    file_buf = psram_malloc(file_size);
    if (!file_buf) {
      ESP_LOGE(TAG, "Failed to allocate %zu bytes for %s", file_size, file_stat.m_filename);
      result = ESP_ERR_NO_MEM;
      break;
    }
    
    if (!mz_zip_reader_extract_to_mem(zip, i, file_buf, file_size, 0)) {
      ESP_LOGE(TAG, "Failed to extract: %s", file_stat.m_filename);
      psram_free(file_buf);
      file_buf = NULL;
      continue;
    }
    
    // Write to filesystem
    FILE *f = fopen(full_path, "wb");
    if (!f) {
      ESP_LOGE(TAG, "Failed to create: %s", full_path);
      psram_free(file_buf);
      file_buf = NULL;
      continue;
    }
    
    size_t written = fwrite(file_buf, 1, file_size, f);
    fclose(f);
    psram_free(file_buf);
    file_buf = NULL;
    
    if (written != file_size) {
      ESP_LOGE(TAG, "Failed to write %s (wrote %zu of %zu)", full_path, written, file_size);
      continue;
    }
    
    ESP_LOGD(TAG, "Extracted: %s (%zu bytes)", file_stat.m_filename, file_size);
  }
  
  mz_zip_reader_end(zip);
  psram_free(zip);
  psram_free(file_path);
  psram_free(full_path);
  
  ESP_LOGI(TAG, "ZIP extraction complete");
  return result;
}

// ============================================================================
// File operation hooks
// ============================================================================

void assets_file_created(const char *path) {
  const char *folder_type = assets_get_folder_type(path);
  if (!folder_type) return;
  
  ESP_LOGD(TAG, "File created in %s folder: %s", folder_type, path);
  
  if (strcmp(folder_type, "scenes") == 0) {
    // Don't regenerate if it's the manifest itself
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_scenes_manifest();
  } else if (strcmp(folder_type, "devices") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_devices_manifest();
    assets_manager_reload_manifest();  // Reload into memory
  } else if (strcmp(folder_type, "images") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_images_manifest();
  }
}

void assets_file_deleted(const char *path) {
  const char *folder_type = assets_get_folder_type(path);
  if (!folder_type) return;
  
  ESP_LOGD(TAG, "File deleted from %s folder: %s", folder_type, path);
  
  if (strcmp(folder_type, "scenes") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_scenes_manifest();
  } else if (strcmp(folder_type, "devices") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_devices_manifest();
    assets_manager_reload_manifest();  // Reload into memory
  } else if (strcmp(folder_type, "images") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_images_manifest();
  }
}

