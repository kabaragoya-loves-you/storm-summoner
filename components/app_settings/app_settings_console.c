#include "app_settings_console.h"
#include "app_settings.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "app_settings_console";
static const char* NVS_NAMESPACE = "app_settings";

static const char* registered_commands[] = {
  "list", "get", "set_u8", "set_u16", "set_u32", "set_bool", "set_str", "erase", "erase_all",
  "dump", "load"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: list - enumerate all NVS keys
static int cmd_list(int argc, char **argv) {
  nvs_iterator_t it = NULL;
  esp_err_t err = nvs_entry_find("nvs", NVS_NAMESPACE, NVS_TYPE_ANY, &it);
  
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "No keys found in namespace '%s'", NVS_NAMESPACE);
    return 0;
  }
  
  ESP_LOGI(TAG, "====== NVS KEYS ======");
  int count = 0;
  
  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    
    const char* type_str = "unknown";
    switch (info.type) {
      case NVS_TYPE_U8: type_str = "u8"; break;
      case NVS_TYPE_I8: type_str = "i8"; break;
      case NVS_TYPE_U16: type_str = "u16"; break;
      case NVS_TYPE_I16: type_str = "i16"; break;
      case NVS_TYPE_U32: type_str = "u32"; break;
      case NVS_TYPE_I32: type_str = "i32"; break;
      case NVS_TYPE_U64: type_str = "u64"; break;
      case NVS_TYPE_I64: type_str = "i64"; break;
      case NVS_TYPE_STR: type_str = "str"; break;
      case NVS_TYPE_BLOB: type_str = "blob"; break;
      default: break;
    }
    
    ESP_LOGI(TAG, "  %s (%s)", info.key, type_str);
    count++;
    
    err = nvs_entry_next(&it);
    if (err != ESP_OK) break;
  }
  
  nvs_release_iterator(it);
  ESP_LOGI(TAG, "Total: %d keys", count);
  ESP_LOGI(TAG, "=====================");
  
  return 0;
}

// Command: get
static struct {
  struct arg_str *key;
  struct arg_end *end;
} get_args;

static int cmd_get(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &get_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, get_args.end, argv[0]);
    return 1;
  }
  
  const char* key = get_args.key->sval[0];
  
  // Try different types
  uint8_t u8_val;
  uint16_t u16_val;
  uint32_t u32_val;
  bool bool_val;
  char str_val[256];
  
  if (app_settings_load_u8(key, &u8_val) == ESP_OK) {
    ESP_LOGI(TAG, "%s = %u (u8)", key, (unsigned)u8_val);
    return 0;
  }
  
  if (app_settings_load_u16(key, &u16_val) == ESP_OK) {
    ESP_LOGI(TAG, "%s = %u (u16)", key, (unsigned)u16_val);
    return 0;
  }
  
  if (app_settings_load_u32(key, &u32_val) == ESP_OK) {
    ESP_LOGI(TAG, "%s = %u (u32)", key, (unsigned)u32_val);
    return 0;
  }
  
  if (app_settings_load_bool(key, &bool_val) == ESP_OK) {
    ESP_LOGI(TAG, "%s = %s (bool)", key, bool_val ? "true" : "false");
    return 0;
  }
  
  if (app_settings_load_str(key, str_val, sizeof(str_val)) == ESP_OK) {
    ESP_LOGI(TAG, "%s = \"%s\" (str)", key, str_val);
    return 0;
  }
  
  ESP_LOGE(TAG, "Key '%s' not found or unsupported type", key);
  return 1;
}

// Command: set_u8
static struct {
  struct arg_str *key;
  struct arg_int *value;
  struct arg_end *end;
} set_u8_args;

static int cmd_set_u8(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_u8_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_u8_args.end, argv[0]);
    return 1;
  }
  
  const char* key = set_u8_args.key->sval[0];
  int val = set_u8_args.value->ival[0];
  
  if (val < 0 || val > 255) {
    ESP_LOGE(TAG, "Value must be 0-255");
    return 1;
  }
  
  esp_err_t ret = app_settings_save_u8(key, (uint8_t)val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set %s = %u", key, (unsigned)val);
  } else {
    ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: set_u16
static struct {
  struct arg_str *key;
  struct arg_int *value;
  struct arg_end *end;
} set_u16_args;

static int cmd_set_u16(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_u16_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_u16_args.end, argv[0]);
    return 1;
  }
  
  const char* key = set_u16_args.key->sval[0];
  int val = set_u16_args.value->ival[0];
  
  if (val < 0 || val > 65535) {
    ESP_LOGE(TAG, "Value must be 0-65535");
    return 1;
  }
  
  esp_err_t ret = app_settings_save_u16(key, (uint16_t)val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set %s = %u", key, (unsigned)val);
  } else {
    ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: set_u32
static struct {
  struct arg_str *key;
  struct arg_int *value;
  struct arg_end *end;
} set_u32_args;

static int cmd_set_u32(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_u32_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_u32_args.end, argv[0]);
    return 1;
  }
  
  const char* key = set_u32_args.key->sval[0];
  int val = set_u32_args.value->ival[0];
  
  esp_err_t ret = app_settings_save_u32(key, (uint32_t)val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set %s = %u", key, (unsigned)val);
  } else {
    ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: set_bool
static struct {
  struct arg_str *key;
  struct arg_str *value;
  struct arg_end *end;
} set_bool_args;

static int cmd_set_bool(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_bool_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_bool_args.end, argv[0]);
    return 1;
  }
  
  const char* key = set_bool_args.key->sval[0];
  const char* val_str = set_bool_args.value->sval[0];
  
  bool val;
  if (strcmp(val_str, "true") == 0 || strcmp(val_str, "1") == 0) {
    val = true;
  } else if (strcmp(val_str, "false") == 0 || strcmp(val_str, "0") == 0) {
    val = false;
  } else {
    ESP_LOGE(TAG, "Value must be true or false");
    return 1;
  }
  
  esp_err_t ret = app_settings_save_bool(key, val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set %s = %s", key, val ? "true" : "false");
  } else {
    ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: set_str
static struct {
  struct arg_str *key;
  struct arg_str *value;
  struct arg_end *end;
} set_str_args;

static int cmd_set_str(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_str_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_str_args.end, argv[0]);
    return 1;
  }
  
  const char* key = set_str_args.key->sval[0];
  const char* val = set_str_args.value->sval[0];
  
  esp_err_t ret = app_settings_save_str(key, val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Set %s = \"%s\"", key, val);
  } else {
    ESP_LOGE(TAG, "Failed to save: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: erase
static struct {
  struct arg_str *key;
  struct arg_end *end;
} erase_args;

static int cmd_erase(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &erase_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, erase_args.end, argv[0]);
    return 1;
  }
  
  const char* key = erase_args.key->sval[0];
  esp_err_t ret = app_settings_erase_key(key);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Erased key: %s", key);
  } else {
    ESP_LOGE(TAG, "Failed to erase: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: erase_all
static int cmd_erase_all(int argc, char **argv) {
  ESP_LOGW(TAG, "This will erase ALL settings!");
  ESP_LOGW(TAG, "To confirm, run: erase_all confirm");
  
  if (argc == 2 && strcmp(argv[1], "confirm") == 0) {
    esp_err_t ret = app_settings_erase_all();
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "All settings erased");
    } else {
      ESP_LOGE(TAG, "Failed to erase all: %s", esp_err_to_name(ret));
    }
    return (ret == ESP_OK) ? 0 : 1;
  }
  
  return 0;
}

// Command: dump - Export all settings as JSON
static int cmd_dump(int argc, char **argv) {
  char* json_str = app_settings_export_json_string(true);  // pretty print
  if (json_str) {
    printf("%s\r\n", json_str);  // No ESP_LOGI - clean output for parsing
    cJSON_free(json_str);
    return 0;
  }
  printf("Error: Failed to export settings\r\n");
  return 1;
}

// Command: load - Import settings from JSON
// Usage: load <json_string>
// Note: For multi-line JSON, use the web interface or Ruby script
static struct {
  struct arg_str *json;
  struct arg_end *end;
} load_args;

static int cmd_load(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &load_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, load_args.end, argv[0]);
    return 1;
  }
  
  const char* json_str = load_args.json->sval[0];
  cJSON* json = cJSON_Parse(json_str);
  if (!json) {
    printf("Error: Invalid JSON\r\n");
    return 1;
  }
  
  int count = app_settings_import_json(json);
  cJSON_Delete(json);
  
  if (count >= 0) {
    printf("Imported %d settings\r\n", count);
    return 0;
  }
  
  printf("Error: Failed to import settings\r\n");
  return 1;
}

esp_err_t app_settings_console_init(void) {
  ESP_LOGI(TAG, "Registering app_settings commands");
  
  // list command
  const esp_console_cmd_t list_cmd = {
    .command = "list",
    .help = "List all NVS keys",
    .hint = NULL,
    .func = &cmd_list,
  };
  esp_console_cmd_register(&list_cmd);
  
  // get command
  get_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  get_args.end = arg_end(2);
  
  const esp_console_cmd_t get_cmd = {
    .command = "get",
    .help = "Get value for key",
    .hint = NULL,
    .func = &cmd_get,
    .argtable = &get_args
  };
  esp_console_cmd_register(&get_cmd);
  
  // set_u8 command
  set_u8_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  set_u8_args.value = arg_int1(NULL, NULL, "<value>", "Value (0-255)");
  set_u8_args.end = arg_end(3);
  
  const esp_console_cmd_t set_u8_cmd = {
    .command = "set_u8",
    .help = "Set uint8_t value",
    .hint = NULL,
    .func = &cmd_set_u8,
    .argtable = &set_u8_args
  };
  esp_console_cmd_register(&set_u8_cmd);
  
  // set_u16 command
  set_u16_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  set_u16_args.value = arg_int1(NULL, NULL, "<value>", "Value (0-65535)");
  set_u16_args.end = arg_end(3);
  
  const esp_console_cmd_t set_u16_cmd = {
    .command = "set_u16",
    .help = "Set uint16_t value",
    .hint = NULL,
    .func = &cmd_set_u16,
    .argtable = &set_u16_args
  };
  esp_console_cmd_register(&set_u16_cmd);
  
  // set_u32 command
  set_u32_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  set_u32_args.value = arg_int1(NULL, NULL, "<value>", "Value");
  set_u32_args.end = arg_end(3);
  
  const esp_console_cmd_t set_u32_cmd = {
    .command = "set_u32",
    .help = "Set uint32_t value",
    .hint = NULL,
    .func = &cmd_set_u32,
    .argtable = &set_u32_args
  };
  esp_console_cmd_register(&set_u32_cmd);
  
  // set_bool command
  set_bool_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  set_bool_args.value = arg_str1(NULL, NULL, "<true|false>", "Boolean value");
  set_bool_args.end = arg_end(3);
  
  const esp_console_cmd_t set_bool_cmd = {
    .command = "set_bool",
    .help = "Set boolean value",
    .hint = NULL,
    .func = &cmd_set_bool,
    .argtable = &set_bool_args
  };
  esp_console_cmd_register(&set_bool_cmd);
  
  // set_str command
  set_str_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  set_str_args.value = arg_str1(NULL, NULL, "<value>", "String value");
  set_str_args.end = arg_end(3);
  
  const esp_console_cmd_t set_str_cmd = {
    .command = "set_str",
    .help = "Set string value",
    .hint = NULL,
    .func = &cmd_set_str,
    .argtable = &set_str_args
  };
  esp_console_cmd_register(&set_str_cmd);
  
  // erase command
  erase_args.key = arg_str1(NULL, NULL, "<key>", "Key name");
  erase_args.end = arg_end(2);
  
  const esp_console_cmd_t erase_cmd = {
    .command = "erase",
    .help = "Erase specific key",
    .hint = NULL,
    .func = &cmd_erase,
    .argtable = &erase_args
  };
  esp_console_cmd_register(&erase_cmd);
  
  // erase_all command
  const esp_console_cmd_t erase_all_cmd = {
    .command = "erase_all",
    .help = "Erase all keys (requires confirmation)",
    .hint = "[confirm]",
    .func = &cmd_erase_all,
  };
  esp_console_cmd_register(&erase_all_cmd);
  
  // dump command
  const esp_console_cmd_t dump_cmd = {
    .command = "dump",
    .help = "Export all settings as JSON",
    .hint = NULL,
    .func = &cmd_dump,
  };
  esp_console_cmd_register(&dump_cmd);
  
  // load command
  load_args.json = arg_str1(NULL, NULL, "<json>", "JSON string to import");
  load_args.end = arg_end(2);
  
  const esp_console_cmd_t load_cmd = {
    .command = "load",
    .help = "Import settings from JSON string",
    .hint = NULL,
    .func = &cmd_load,
    .argtable = &load_args
  };
  esp_console_cmd_register(&load_cmd);
  
  return ESP_OK;
}

void app_settings_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering app_settings commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

