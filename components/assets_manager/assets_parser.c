#include "assets_manager.h"
#include "assets_types.h"
#include "memory_utils.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>

#define TAG "assets_parser"

// Forward declarations
extern device_def_t *parse_device_json(const char *json_str, size_t json_len, const char *slug);

static bool cc_array_has_duplicate_numbers(cJSON *cc_array) {
  if (!cc_array || !cJSON_IsArray(cc_array)) {
    return false;
  }

  bool seen[128] = {false};
  cJSON *item = NULL;
  cJSON_ArrayForEach(item, cc_array) {
    cJSON *cc_num = cJSON_GetObjectItem(item, "controlChangeNumber");
    if (!cc_num || !cJSON_IsNumber(cc_num)) {
      continue;
    }
    int id = cc_num->valueint;
    if (id < 0 || id > 127) {
      continue;
    }
    if (seen[id]) {
      return true;
    }
    seen[id] = true;
  }
  return false;
}

// Map an x_variants constraint operator string to cc_variant_op_t.
// Returns -1 for an unrecognized operator.
static int parse_variant_op(const char *op) {
  if (!op) return -1;
  if (strcmp(op, "<") == 0) return CC_VARIANT_OP_LT;
  if (strcmp(op, "<=") == 0) return CC_VARIANT_OP_LE;
  if (strcmp(op, ">") == 0) return CC_VARIANT_OP_GT;
  if (strcmp(op, ">=") == 0) return CC_VARIANT_OP_GE;
  if (strcmp(op, "==") == 0) return CC_VARIANT_OP_EQ;
  if (strcmp(op, "!=") == 0) return CC_VARIANT_OP_NE;
  return -1;
}

// Number of discrete entries a valueRange would yield, capped to the max.
static int range_discrete_count(cJSON *range) {
  if (!range) return 0;
  cJSON *arr = cJSON_GetObjectItem(range, "discreteValues");
  if (!arr || !cJSON_IsArray(arr)) return 0;
  int n = cJSON_GetArraySize(arr);
  return (n > MAX_DISCRETE_VALUES) ? MAX_DISCRETE_VALUES : n;
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
  
  free(json_buf);
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
  device->trs_type = MIDI_TRS_UNKNOWN;  // Default (caller should treat as BOTH)
  cJSON *midi_trs = cJSON_GetObjectItem(root, "x_midiTrs");
  if (midi_trs && cJSON_IsString(midi_trs)) {
    const char *trs_str = midi_trs->valuestring;
    if (strcmp(trs_str, "TYPE_A") == 0)
      device->trs_type = MIDI_TRS_TYPE_A;
    else if (strcmp(trs_str, "TYPE_B") == 0)
      device->trs_type = MIDI_TRS_TYPE_B;
    else if (strcmp(trs_str, "TYPE_TS") == 0)
      device->trs_type = MIDI_TRS_TYPE_TS;
    else if (strcmp(trs_str, "BOTH") == 0)
      device->trs_type = MIDI_TRS_TYPE_BOTH;
  }
  
  // Parse x_midiChannel extension (1-16, 0 means not specified)
  device->midi_channel = 0;  // Default: not specified
  cJSON *midi_channel = cJSON_GetObjectItem(root, "x_midiChannel");
  if (midi_channel && cJSON_IsNumber(midi_channel)) {
    int ch = midi_channel->valueint;
    if (ch >= 1 && ch <= 16)
      device->midi_channel = (uint8_t)ch;
  }
  
  // Parse receives/transmits arrays
  cJSON *receives = cJSON_GetObjectItem(root, "receives");
  if (receives && cJSON_IsArray(receives)) {
    cJSON *item;
    cJSON_ArrayForEach(item, receives) {
      if (cJSON_IsString(item)) {
        if (strcmp(item->valuestring, "PROGRAM_CHANGE") == 0)
          device->receives_pc = true;
        else if (strcmp(item->valuestring, "CLOCK") == 0)
          device->receives_clock = true;
        else if (strcmp(item->valuestring, "NOTE_ON") == 0 ||
                 strcmp(item->valuestring, "NOTE_OFF") == 0)
          device->receives_notes = true;
      }
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
    if (cc_array_has_duplicate_numbers(cc_array)) {
      ESP_LOGE(TAG, "Duplicate controlChangeNumber in device '%s'", slug);
      cJSON_Delete(root);
      free(device);
      return NULL;
    }

    device->control_count = cJSON_GetArraySize(cc_array);
    
    if (device->control_count > 0) {
      // Allocate controls array in PSRAM
      device->controls = calloc_prefer_psram(device->control_count, sizeof(midi_control_t));
      if (!device->controls) {
        ESP_LOGE(TAG, "Failed to allocate controls array");
        cJSON_Delete(root);
        free(device);
        return NULL;
      }
      
      // Calculate total string size needed (including discrete value names)
      size_t string_size = 0;
      size_t total_discrete_count = 0;
      size_t total_variant_count = 0;
      size_t total_variant_discrete_count = 0;
      cJSON *cc_item;
      cJSON_ArrayForEach(cc_item, cc_array) {
        cJSON *name = cJSON_GetObjectItem(cc_item, "name");
        if (name && cJSON_IsString(name))
          string_size += strlen(name->valuestring) + 1;
        
        cJSON *info = cJSON_GetObjectItem(cc_item, "additionalInfo");
        if (info && cJSON_IsString(info))
          string_size += strlen(info->valuestring) + 1;
        
        // Count discrete values and their string sizes
        int base_dv_count = 0;
        cJSON *range = cJSON_GetObjectItem(cc_item, "valueRange");
        if (range) {
          cJSON *discrete_arr = cJSON_GetObjectItem(range, "discreteValues");
          if (discrete_arr && cJSON_IsArray(discrete_arr)) {
            base_dv_count = range_discrete_count(range);
            total_discrete_count += base_dv_count;
            
            cJSON *dv_item;
            int dv_idx = 0;
            cJSON_ArrayForEach(dv_item, discrete_arr) {
              if (dv_idx >= MAX_DISCRETE_VALUES) break;
              cJSON *dv_name = cJSON_GetObjectItem(dv_item, "name");
              if (dv_name && cJSON_IsString(dv_name))
                string_size += strlen(dv_name->valuestring) + 1;
              dv_idx++;
            }
          }
        }
        
        // Count x_variants and their string/discrete sizes. A variant with
        // its own valueRange materializes its own discrete set; a variant
        // without a valueRange inherits the base discrete set (its names are
        // already in the blob, so no new string bytes are needed for them).
        cJSON *variants = cJSON_GetObjectItem(cc_item, "x_variants");
        if (variants && cJSON_IsArray(variants)) {
          cJSON *var_item;
          int var_idx = 0;
          cJSON_ArrayForEach(var_item, variants) {
            if (var_idx >= MAX_VARIANTS) break;
            var_idx++;
            total_variant_count++;
            
            cJSON *v_name = cJSON_GetObjectItem(var_item, "name");
            if (v_name && cJSON_IsString(v_name))
              string_size += strlen(v_name->valuestring) + 1;
            
            cJSON *v_range = cJSON_GetObjectItem(var_item, "valueRange");
            if (v_range) {
              cJSON *v_disc = cJSON_GetObjectItem(v_range, "discreteValues");
              if (v_disc && cJSON_IsArray(v_disc)) {
                int vc = range_discrete_count(v_range);
                total_variant_discrete_count += vc;
                cJSON *dv_item;
                int dv_idx = 0;
                cJSON_ArrayForEach(dv_item, v_disc) {
                  if (dv_idx >= MAX_DISCRETE_VALUES) break;
                  cJSON *dv_name = cJSON_GetObjectItem(dv_item, "name");
                  if (dv_name && cJSON_IsString(dv_name))
                    string_size += strlen(dv_name->valuestring) + 1;
                  dv_idx++;
                }
              }
              // valueRange without discreteValues = continuous override (0)
            } else {
              // Inherits the base discrete set (names reuse base strings)
              total_variant_discrete_count += base_dv_count;
            }
          }
        }
      }
      
      // Allocate string blob in PSRAM
      device->string_blob_size = string_size;
      device->string_blob = malloc_prefer_psram(string_size);
      if (!device->string_blob) {
        ESP_LOGE(TAG, "Failed to allocate string blob");
        free(device->controls);
        free(device);
        cJSON_Delete(root);
        return NULL;
      }
      
      // Allocate discrete values array if needed
      discrete_value_t *all_discrete = NULL;
      discrete_value_t *discrete_ptr = NULL;
      if (total_discrete_count > 0) {
        all_discrete = calloc_prefer_psram(total_discrete_count, sizeof(discrete_value_t));
        discrete_ptr = all_discrete;
        if (!all_discrete) {
          ESP_LOGW(TAG, "Failed to allocate discrete values, continuing without them");
        }
      }
      
      // Allocate variant pools if needed
      cc_variant_t *all_variants = NULL;
      cc_variant_t *variant_ptr = NULL;
      if (total_variant_count > 0) {
        all_variants = calloc_prefer_psram(total_variant_count, sizeof(cc_variant_t));
        variant_ptr = all_variants;
        if (!all_variants)
          ESP_LOGW(TAG, "Failed to allocate variants, continuing without them");
      }
      discrete_value_t *all_variant_discrete = NULL;
      discrete_value_t *variant_discrete_ptr = NULL;
      if (total_variant_discrete_count > 0) {
        all_variant_discrete = calloc_prefer_psram(total_variant_discrete_count, sizeof(discrete_value_t));
        variant_discrete_ptr = all_variant_discrete;
        if (!all_variant_discrete)
          ESP_LOGW(TAG, "Failed to allocate variant discrete values, continuing without them");
      }

      // Record the owning bases so assets_free_device can release these pools.
      device->discrete_pool = all_discrete;
      device->variant_pool = all_variants;
      device->variant_discrete_pool = all_variant_discrete;
      
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
        
        // Get value range (default to 0-127 for CC)
        ctrl->min = 0;
        ctrl->max = 127;
        cJSON *range = cJSON_GetObjectItem(cc_item, "valueRange");
        if (range) {
          cJSON *min = cJSON_GetObjectItem(range, "min");
          cJSON *max = cJSON_GetObjectItem(range, "max");
          
          if (min && cJSON_IsNumber(min))
            ctrl->min = min->valueint;
          if (max && cJSON_IsNumber(max))
            ctrl->max = max->valueint;
          
          // Parse discrete values
          cJSON *discrete_arr = cJSON_GetObjectItem(range, "discreteValues");
          if (discrete_arr && cJSON_IsArray(discrete_arr) && discrete_ptr) {
            int dv_count = cJSON_GetArraySize(discrete_arr);
            if (dv_count > MAX_DISCRETE_VALUES)
              dv_count = MAX_DISCRETE_VALUES;
            
            if (dv_count > 0) {
              ctrl->discrete_values = discrete_ptr;
              ctrl->discrete_count = 0;
              
              cJSON *dv_item;
              cJSON_ArrayForEach(dv_item, discrete_arr) {
                if (ctrl->discrete_count >= MAX_DISCRETE_VALUES) break;
                
                cJSON *dv_name = cJSON_GetObjectItem(dv_item, "name");
                cJSON *dv_value = cJSON_GetObjectItem(dv_item, "value");
                
                if (dv_name && cJSON_IsString(dv_name) && dv_value && cJSON_IsNumber(dv_value)) {
                  size_t len = strlen(dv_name->valuestring) + 1;
                  memcpy(string_ptr, dv_name->valuestring, len);
                  discrete_ptr->name = string_ptr;
                  discrete_ptr->value = dv_value->valueint;
                  string_ptr += len;
                  discrete_ptr++;
                  ctrl->discrete_count++;
                }
              }
            }
          }
        }

        // x_noop on the base control: hide the CC when no variant matches.
        cJSON *base_noop = cJSON_GetObjectItem(cc_item, "x_noop");
        ctrl->noop = (base_noop && cJSON_IsTrue(base_noop)) ? 1 : 0;

        // x_mandatory: this CC must always carry a per-scene default value.
        cJSON *mandatory = cJSON_GetObjectItem(cc_item, "x_mandatory");
        if (mandatory && cJSON_IsTrue(mandatory))
          ctrl->flags |= MIDI_CONTROL_FLAG_MANDATORY;
        
        // Parse x_variants (mode-dependent overrides). Each variant's
        // inherited fields are materialized now so the runtime resolver and
        // the binary cache always see fully-populated variant records.
        cJSON *variants = cJSON_GetObjectItem(cc_item, "x_variants");
        if (variants && cJSON_IsArray(variants) && variant_ptr) {
          cc_variant_t *first_variant = variant_ptr;
          uint8_t vcount = 0;
          int var_pos = 0;
          cJSON *var_item;
          cJSON_ArrayForEach(var_item, variants) {
            if (var_pos >= MAX_VARIANTS) break;
            var_pos++;
            
            cJSON *constraint = cJSON_GetObjectItem(var_item, "constraint");
            if (!constraint) continue;
            cJSON *c_cc = cJSON_GetObjectItem(constraint, "cc");
            cJSON *c_op = cJSON_GetObjectItem(constraint, "op");
            cJSON *c_val = cJSON_GetObjectItem(constraint, "value");
            if (!c_cc || !cJSON_IsNumber(c_cc) || !c_op || !cJSON_IsString(c_op) ||
                !c_val || !cJSON_IsNumber(c_val))
              continue;
            int op = parse_variant_op(c_op->valuestring);
            if (op < 0) continue;
            
            cc_variant_t *v = variant_ptr;
            v->gating_cc = (uint8_t)c_cc->valueint;
            v->op = (uint8_t)op;
            v->value = (uint16_t)c_val->valueint;

            cJSON *v_noop = cJSON_GetObjectItem(var_item, "x_noop");
            v->noop = (v_noop && cJSON_IsTrue(v_noop)) ? 1 : 0;
            
            // Name: variant's own name, else inherit the base control name
            cJSON *v_name = cJSON_GetObjectItem(var_item, "name");
            if (v_name && cJSON_IsString(v_name)) {
              size_t len = strlen(v_name->valuestring) + 1;
              memcpy(string_ptr, v_name->valuestring, len);
              v->name = string_ptr;
              string_ptr += len;
            } else {
              v->name = ctrl->name;
            }
            
            cJSON *v_range = cJSON_GetObjectItem(var_item, "valueRange");
            if (v_range) {
              v->min = ctrl->min;
              v->max = ctrl->max;
              cJSON *vmin = cJSON_GetObjectItem(v_range, "min");
              cJSON *vmax = cJSON_GetObjectItem(v_range, "max");
              if (vmin && cJSON_IsNumber(vmin)) v->min = vmin->valueint;
              if (vmax && cJSON_IsNumber(vmax)) v->max = vmax->valueint;
              
              cJSON *v_disc = cJSON_GetObjectItem(v_range, "discreteValues");
              if (v_disc && cJSON_IsArray(v_disc) && variant_discrete_ptr) {
                v->discrete_values = variant_discrete_ptr;
                v->discrete_count = 0;
                cJSON *dv_item;
                cJSON_ArrayForEach(dv_item, v_disc) {
                  if (v->discrete_count >= MAX_DISCRETE_VALUES) break;
                  cJSON *dv_name = cJSON_GetObjectItem(dv_item, "name");
                  cJSON *dv_value = cJSON_GetObjectItem(dv_item, "value");
                  if (dv_name && cJSON_IsString(dv_name) && dv_value && cJSON_IsNumber(dv_value)) {
                    size_t len = strlen(dv_name->valuestring) + 1;
                    memcpy(string_ptr, dv_name->valuestring, len);
                    variant_discrete_ptr->name = string_ptr;
                    variant_discrete_ptr->value = dv_value->valueint;
                    string_ptr += len;
                    variant_discrete_ptr++;
                    v->discrete_count++;
                  }
                }
                if (v->discrete_count == 0) v->discrete_values = NULL;
              } else {
                // valueRange without discreteValues = continuous in this mode
                v->discrete_values = NULL;
                v->discrete_count = 0;
              }
            } else {
              // No valueRange: inherit the base range and discrete set
              v->min = ctrl->min;
              v->max = ctrl->max;
              if (ctrl->discrete_count > 0 && ctrl->discrete_values && variant_discrete_ptr) {
                v->discrete_values = variant_discrete_ptr;
                v->discrete_count = ctrl->discrete_count;
                for (uint8_t k = 0; k < ctrl->discrete_count; k++) {
                  variant_discrete_ptr->name = ctrl->discrete_values[k].name;
                  variant_discrete_ptr->value = ctrl->discrete_values[k].value;
                  variant_discrete_ptr++;
                }
              } else {
                v->discrete_values = NULL;
                v->discrete_count = 0;
              }
            }
            
            variant_ptr++;
            vcount++;
          }
          
          if (vcount > 0) {
            ctrl->variants = first_variant;
            ctrl->variant_count = vcount;
          }
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
      // Set defaults
      device->pc_info->index_base = 0;
      device->pc_info->count = 128;
      device->pc_info->bank_mode = PC_BANK_SELECT_NONE;
      
      cJSON *index_base = cJSON_GetObjectItem(x_pc, "indexBase");
      if (index_base && cJSON_IsNumber(index_base))
        device->pc_info->index_base = index_base->valueint;
      
      cJSON *count = cJSON_GetObjectItem(x_pc, "count");
      if (count && cJSON_IsNumber(count))
        device->pc_info->count = count->valueint;
      
      // Parse bankSelectMode - string values: "CC0", "CC0_CC32", or omit for none
      cJSON *bank_sel = cJSON_GetObjectItem(x_pc, "bankSelectMode");
      if (bank_sel && cJSON_IsString(bank_sel)) {
        const char *bs_str = bank_sel->valuestring;
        if (strcmp(bs_str, "CC0") == 0)
          device->pc_info->bank_mode = PC_BANK_SELECT_CC0;
        else if (strcmp(bs_str, "CC0_CC32") == 0)
          device->pc_info->bank_mode = PC_BANK_SELECT_CC0_CC32;
        // Any other value stays as PC_BANK_SELECT_NONE
      }
      
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
          device->pc_names_blob = names_blob;
        }
      }
    }
  }
  
  cJSON_Delete(root);
  
  ESP_LOGI(TAG, "Parsed device '%s': %u controls", device->slug, (unsigned)device->control_count);
  
  return device;
}

esp_err_t assets_validate_device_json_file(const char *filepath) {
  if (!filepath) {
    return ESP_ERR_INVALID_ARG;
  }

  FILE *f = fopen(filepath, "rb");
  if (!f) {
    return ESP_ERR_NOT_FOUND;
  }

  struct stat st;
  if (stat(filepath, &st) != 0) {
    fclose(f);
    return ESP_FAIL;
  }

  size_t file_size = (size_t)st.st_size;
  char *json_buf = malloc_prefer_psram(file_size + 1);
  if (!json_buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  size_t read_bytes = fread(json_buf, 1, file_size, f);
  fclose(f);
  if (read_bytes != file_size) {
    heap_caps_free(json_buf);
    return ESP_FAIL;
  }
  json_buf[file_size] = '\0';

  cJSON *root = cJSON_Parse(json_buf);
  heap_caps_free(json_buf);
  if (!root) {
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *cc_array = cJSON_GetObjectItem(root, "controlChangeCommands");
  esp_err_t err = ESP_OK;
  if (cc_array_has_duplicate_numbers(cc_array)) {
    ESP_LOGE(TAG, "Duplicate controlChangeNumber in %s", filepath);
    err = ESP_ERR_INVALID_STATE;
  }

  cJSON_Delete(root);
  return err;
}

