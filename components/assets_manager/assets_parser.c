#include "assets_manager.h"
#include "assets_types.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>

#define TAG "assets_parser"

// Forward declarations
extern device_def_t *parse_device_json(const char *json_str, size_t json_len, const char *slug);

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
 * Parse a device JSON file from filesystem
 */
device_def_t *assets_parse_device_file(const char *filepath, const char *slug) {
  ESP_LOGI(TAG, "Parsing device file: %s", filepath);
  
  // Open and read file
  FILE *f = fopen(filepath, "rb");
  if (!f) {
    ESP_LOGE(TAG, "Failed to open %s", filepath);
    return NULL;
  }
  
  // Get file size
  struct stat st;
  if (stat(filepath, &st) != 0) {
    ESP_LOGE(TAG, "Failed to stat %s", filepath);
    fclose(f);
    return NULL;
  }
  
  size_t file_size = st.st_size;
  
  // Allocate buffer in PSRAM for JSON
  char *json_buf = malloc_prefer_psram(file_size + 1);
  if (!json_buf) {
    ESP_LOGE(TAG, "Failed to allocate %u bytes for JSON", (unsigned)file_size);
    fclose(f);
    return NULL;
  }
  
  // Read file
  size_t read_bytes = fread(json_buf, 1, file_size, f);
  fclose(f);
  
  if (read_bytes != file_size) {
    ESP_LOGE(TAG, "Failed to read file (got %u of %u bytes)", (unsigned)read_bytes, (unsigned)file_size);
    heap_caps_free(json_buf);
    return NULL;
  }
  
  json_buf[file_size] = '\0';
  
  // Parse JSON
  device_def_t *device = parse_device_json(json_buf, file_size, slug);
  
  free_smart(json_buf);
  return device;
}

/**
 * Parse device JSON string into device_def_t
 */
device_def_t *parse_device_json(const char *json_str, size_t json_len, const char *slug) {
  cJSON *root = cJSON_Parse(json_str);
  if (!root) {
    ESP_LOGE(TAG, "Failed to parse JSON");
    return NULL;
  }
  
  // Allocate device definition in PSRAM
  device_def_t *device = calloc_prefer_psram(1, sizeof(device_def_t));
  if (!device) {
    ESP_LOGE(TAG, "Failed to allocate device_def_t");
    cJSON_Delete(root);
    return NULL;
  }
  
  // Copy slug
  strncpy(device->slug, slug, sizeof(device->slug) - 1);
  
  // Parse device metadata
  cJSON *dev_obj = cJSON_GetObjectItem(root, "device");
  if (dev_obj) {
    cJSON *item;
    
    item = cJSON_GetObjectItem(dev_obj, "manufacturer");
    if (item && cJSON_IsString(item))
      strncpy(device->vendor, item->valuestring, sizeof(device->vendor) - 1);
    
    item = cJSON_GetObjectItem(dev_obj, "model");
    if (item && cJSON_IsString(item))
      strncpy(device->model, item->valuestring, sizeof(device->model) - 1);
    
    item = cJSON_GetObjectItem(dev_obj, "version");
    if (item && cJSON_IsString(item))
      strncpy(device->version, item->valuestring, sizeof(device->version) - 1);
  }
  
  // Parse displayName
  cJSON *display_name = cJSON_GetObjectItem(root, "displayName");
  if (display_name && cJSON_IsString(display_name))
    strncpy(device->name, display_name->valuestring, sizeof(device->name) - 1);
  
  // Parse x_midiTrs extension
  device->trs_type = MIDI_TRS_UNKNOWN;  // Default
  cJSON *midi_trs = cJSON_GetObjectItem(root, "x_midiTrs");
  if (midi_trs && cJSON_IsString(midi_trs)) {
    const char *trs_str = midi_trs->valuestring;
    if (strcmp(trs_str, "TYPE_A") == 0)
      device->trs_type = MIDI_TRS_TYPE_A;
    else if (strcmp(trs_str, "TYPE_B") == 0)
      device->trs_type = MIDI_TRS_TYPE_B;
    else if (strcmp(trs_str, "TYPE_CS") == 0)
      device->trs_type = MIDI_TRS_TYPE_CS;
  }
  
  // Parse receives/transmits arrays
  cJSON *receives = cJSON_GetObjectItem(root, "receives");
  if (receives && cJSON_IsArray(receives)) {
    cJSON *item;
    cJSON_ArrayForEach(item, receives) {
      if (cJSON_IsString(item) && strcmp(item->valuestring, "PROGRAM_CHANGE") == 0)
        device->receives_pc = true;
    }
  }
  
  cJSON *transmits = cJSON_GetObjectItem(root, "transmits");
  if (transmits && cJSON_IsArray(transmits)) {
    cJSON *item;
    cJSON_ArrayForEach(item, transmits) {
      if (cJSON_IsString(item) && strcmp(item->valuestring, "PROGRAM_CHANGE") == 0)
        device->transmits_pc = true;
    }
  }
  
  // Parse controlChangeCommands
  cJSON *cc_array = cJSON_GetObjectItem(root, "controlChangeCommands");
  if (cc_array && cJSON_IsArray(cc_array)) {
    device->control_count = cJSON_GetArraySize(cc_array);
    
    if (device->control_count > 0) {
      // Allocate controls array in PSRAM
      device->controls = calloc_prefer_psram(device->control_count, sizeof(midi_control_t));
      if (!device->controls) {
        ESP_LOGE(TAG, "Failed to allocate controls array");
        cJSON_Delete(root);
        free_smart(device);
        return NULL;
      }
      
      // Calculate total string size needed
      size_t string_size = 0;
      cJSON *cc_item;
      cJSON_ArrayForEach(cc_item, cc_array) {
        cJSON *name = cJSON_GetObjectItem(cc_item, "name");
        if (name && cJSON_IsString(name))
          string_size += strlen(name->valuestring) + 1;
        
        cJSON *info = cJSON_GetObjectItem(cc_item, "additionalInfo");
        if (info && cJSON_IsString(info))
          string_size += strlen(info->valuestring) + 1;
      }
      
      // Allocate string blob in PSRAM
      device->string_blob_size = string_size;
      device->string_blob = malloc_prefer_psram(string_size);
      if (!device->string_blob) {
        ESP_LOGE(TAG, "Failed to allocate string blob");
        free_smart(device->controls);
        free_smart(device);
        cJSON_Delete(root);
        return NULL;
      }
      
      // Parse each control
      char *string_ptr = (char *)device->string_blob;
      int idx = 0;
      cJSON_ArrayForEach(cc_item, cc_array) {
        midi_control_t *ctrl = &device->controls[idx];
        
        // Get CC number
        cJSON *cc_num = cJSON_GetObjectItem(cc_item, "controlChangeNumber");
        if (cc_num && cJSON_IsNumber(cc_num)) {
          ctrl->type = MIDI_CONTROL_TYPE_CC;
          ctrl->id = cc_num->valueint;
        }
        
        // Get name
        cJSON *name = cJSON_GetObjectItem(cc_item, "name");
        if (name && cJSON_IsString(name)) {
          size_t len = strlen(name->valuestring) + 1;
          memcpy(string_ptr, name->valuestring, len);
          ctrl->name = string_ptr;
          string_ptr += len;
        }
        
        // Get additional info
        cJSON *info = cJSON_GetObjectItem(cc_item, "additionalInfo");
        if (info && cJSON_IsString(info)) {
          size_t len = strlen(info->valuestring) + 1;
          memcpy(string_ptr, info->valuestring, len);
          ctrl->additional_info = string_ptr;
          string_ptr += len;
        }
        
        // Get value range
        cJSON *range = cJSON_GetObjectItem(cc_item, "valueRange");
        if (range) {
          cJSON *min = cJSON_GetObjectItem(range, "min");
          cJSON *max = cJSON_GetObjectItem(range, "max");
          
          if (min && cJSON_IsNumber(min))
            ctrl->min = min->valueint;
          if (max && cJSON_IsNumber(max))
            ctrl->max = max->valueint;
        }
        
        idx++;
      }
    }
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
  
  // Parse x_pc extension
  cJSON *x_pc = cJSON_GetObjectItem(root, "x_pc");
  if (x_pc) {
    device->pc_info = calloc_prefer_psram(1, sizeof(program_change_info_t));
    if (device->pc_info) {
      cJSON *index_base = cJSON_GetObjectItem(x_pc, "indexBase");
      if (index_base && cJSON_IsNumber(index_base))
        device->pc_info->index_base = index_base->valueint;
      
      cJSON *count = cJSON_GetObjectItem(x_pc, "count");
      if (count && cJSON_IsNumber(count))
        device->pc_info->count = count->valueint;
      
      cJSON *bank_sel = cJSON_GetObjectItem(x_pc, "bankSelect");
      if (bank_sel && cJSON_IsBool(bank_sel))
        device->pc_info->bank_select = cJSON_IsTrue(bank_sel);
      
      // Parse names array if present
      cJSON *names = cJSON_GetObjectItem(x_pc, "names");
      if (names && cJSON_IsArray(names)) {
        int name_count = cJSON_GetArraySize(names);
        
        // Calculate string size
        size_t pc_string_size = 0;
        cJSON *name_item;
        cJSON_ArrayForEach(name_item, names) {
          if (cJSON_IsString(name_item))
            pc_string_size += strlen(name_item->valuestring) + 1;
        }
        
        // Allocate names array and string blob
        char **names_arr = malloc_prefer_psram(name_count * sizeof(char *));
        char *names_blob = malloc_prefer_psram(pc_string_size);
        
        if (names_arr && names_blob) {
          char *name_ptr = names_blob;
          int i = 0;
          cJSON_ArrayForEach(name_item, names) {
            if (cJSON_IsString(name_item)) {
              size_t len = strlen(name_item->valuestring) + 1;
              memcpy(name_ptr, name_item->valuestring, len);
              names_arr[i++] = name_ptr;
              name_ptr += len;
            }
          }
          device->pc_info->names = (const char **)names_arr;
        }
      }
    }
  }
  
  cJSON_Delete(root);
  
  ESP_LOGI(TAG, "Parsed device '%s': %u controls", device->slug, (unsigned)device->control_count);
  
  return device;
}

