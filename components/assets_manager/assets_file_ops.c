#include "assets_file_ops.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "assets_file_ops"
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
    
    char *json_buf = malloc(fsize + 1);
    if (!json_buf) {
      fclose(f);
      continue;
    }
    
    fread(json_buf, 1, fsize, f);
    fclose(f);
    json_buf[fsize] = '\0';
    
    // Parse to get name
    cJSON *scene_json = cJSON_Parse(json_buf);
    free(json_buf);
    
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

esp_err_t assets_regenerate_devices_manifest(void) {
  const char *devices_dir = ASSETS_BASE_PATH "/devices";
  const char *manifest_path = ASSETS_BASE_PATH "/devices/manifest.json";
  
  ESP_LOGI(TAG, "Regenerating devices manifest");
  
  DIR *dir = opendir(devices_dir);
  if (!dir) {
    ESP_LOGW(TAG, "Cannot open devices directory");
    return ESP_ERR_NOT_FOUND;
  }
  
  // Create JSON structure
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "schema", 1);
  cJSON *devices = cJSON_CreateArray();
  int count = 0;
  
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip non-JSON files and manifest
    char *ext = strstr(entry->d_name, ".json");
    if (!ext || strcmp(ext, ".json") != 0) continue;
    if (strcmp(entry->d_name, "manifest.json") == 0) continue;
    
    // Build full path
    char device_path[MAX_PATH_LEN];
    snprintf(device_path, sizeof(device_path), "%s/%s", devices_dir, entry->d_name);
    
    struct stat st;
    if (stat(device_path, &st) != 0) continue;
    
    // Read device file to extract metadata
    FILE *f = fopen(device_path, "r");
    if (!f) continue;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize > 65536) {  // Sanity check
      fclose(f);
      continue;
    }
    
    char *json_buf = malloc(fsize + 1);
    if (!json_buf) {
      fclose(f);
      continue;
    }
    
    fread(json_buf, 1, fsize, f);
    fclose(f);
    json_buf[fsize] = '\0';
    
    cJSON *device_json = cJSON_Parse(json_buf);
    free(json_buf);
    
    if (!device_json) continue;
    
    // Extract metadata
    cJSON *slug_item = cJSON_GetObjectItem(device_json, "slug");
    cJSON *name_item = cJSON_GetObjectItem(device_json, "name");
    cJSON *vendor_item = cJSON_GetObjectItem(device_json, "vendor");
    cJSON *version_item = cJSON_GetObjectItem(device_json, "version");
    
    // Build manifest entry
    cJSON *entry_obj = cJSON_CreateObject();
    
    if (slug_item && cJSON_IsString(slug_item)) {
      cJSON_AddStringToObject(entry_obj, "slug", slug_item->valuestring);
    } else {
      // Generate slug from filename
      char slug[64];
      strncpy(slug, entry->d_name, sizeof(slug) - 1);
      char *dot = strrchr(slug, '.');
      if (dot) *dot = '\0';
      cJSON_AddStringToObject(entry_obj, "slug", slug);
    }
    
    if (name_item && cJSON_IsString(name_item)) {
      cJSON_AddStringToObject(entry_obj, "name", name_item->valuestring);
    }
    if (vendor_item && cJSON_IsString(vendor_item)) {
      cJSON_AddStringToObject(entry_obj, "vendor", vendor_item->valuestring);
    }
    if (version_item && cJSON_IsString(version_item)) {
      cJSON_AddStringToObject(entry_obj, "version", version_item->valuestring);
    }
    
    cJSON_AddStringToObject(entry_obj, "file", entry->d_name);
    cJSON_AddNumberToObject(entry_obj, "size", (double)st.st_size);
    
    cJSON_AddItemToArray(devices, entry_obj);
    cJSON_Delete(device_json);
    count++;
  }
  
  closedir(dir);
  
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
  } else if (strcmp(folder_type, "images") == 0) {
    if (strstr(path, "manifest.json")) return;
    assets_regenerate_images_manifest();
  }
}

