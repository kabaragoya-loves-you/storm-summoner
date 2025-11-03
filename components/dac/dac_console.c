#include "dac_console.h"
#include "dac.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "dac_console";

static const char* registered_commands[] = {
  "info", "set", "range"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t value = 0;
  mcp4725_cv_range_t range;
  
  dac_get_value(&value);
  dac_get_cv_range(&range);
  
  const char* range_str;
  switch (range) {
    case MCP4725_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case MCP4725_RANGE_10V: range_str = "0-10V"; break;
    case MCP4725_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case MCP4725_RANGE_5V: range_str = "0-5V"; break;
    case MCP4725_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  float vref = dac_get_vref();
  float voltage = dac_value_to_voltage(value, vref);
  
  ESP_LOGI(TAG, "====== DAC ======");
  ESP_LOGI(TAG, "Value: %u (0-4095)", (unsigned)value);
  ESP_LOGI(TAG, "Voltage: %.3f V", voltage);
  ESP_LOGI(TAG, "Range: %s", range_str);
  ESP_LOGI(TAG, "VREF: %.3f V", vref);
  ESP_LOGI(TAG, "=================");
  
  return 0;
}

// Command: set
static struct {
  struct arg_int *value;
  struct arg_end *end;
} set_args;

static int cmd_set(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_args.end, argv[0]);
    return 1;
  }
  
  int val = set_args.value->ival[0];
  if (val < 0 || val > 4095) {
    ESP_LOGE(TAG, "Value must be 0-4095");
    return 1;
  }
  
  esp_err_t ret = dac_set_value((uint16_t)val);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "DAC value set to %d", val);
  } else {
    ESP_LOGE(TAG, "Failed to set DAC value: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: range
static struct {
  struct arg_str *range_type;
  struct arg_end *end;
} range_args;

static int cmd_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, range_args.end, argv[0]);
    return 1;
  }
  
  const char* range_str = range_args.range_type->sval[0];
  mcp4725_cv_range_t range;
  
  if (strcmp(range_str, "10v") == 0) {
    range = MCP4725_RANGE_10V;
  } else if (strcmp(range_str, "bi10v") == 0) {
    range = MCP4725_RANGE_BIPOLAR_10V;
  } else if (strcmp(range_str, "5v") == 0) {
    range = MCP4725_RANGE_5V;
  } else if (strcmp(range_str, "bi5v") == 0) {
    range = MCP4725_RANGE_BIPOLAR_5V;
  } else if (strcmp(range_str, "3v3") == 0) {
    range = MCP4725_RANGE_3V3;
  } else {
    ESP_LOGE(TAG, "Unknown range. Use: 10v, bi10v, 5v, bi5v, or 3v3");
    return 1;
  }
  
  esp_err_t ret = dac_set_cv_range(range);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "DAC range set to: %s", range_str);
  } else {
    ESP_LOGE(TAG, "Failed to set range: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t dac_console_init(void) {
  ESP_LOGI(TAG, "Registering dac commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show DAC status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // set command
  set_args.value = arg_int1(NULL, NULL, "<0-4095>", "DAC value");
  set_args.end = arg_end(2);
  
  const esp_console_cmd_t set_cmd = {
    .command = "set",
    .help = "Set DAC output value",
    .hint = NULL,
    .func = &cmd_set,
    .argtable = &set_args
  };
  esp_console_cmd_register(&set_cmd);
  
  // range command
  range_args.range_type = arg_str1(NULL, NULL, "<10v|bi10v|5v|bi5v|3v3>", "CV range");
  range_args.end = arg_end(2);
  
  const esp_console_cmd_t range_cmd = {
    .command = "range",
    .help = "Set CV output range",
    .hint = NULL,
    .func = &cmd_range,
    .argtable = &range_args
  };
  esp_console_cmd_register(&range_cmd);
  
  return ESP_OK;
}

void dac_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering dac commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

