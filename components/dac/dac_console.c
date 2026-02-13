#include "dac_console.h"
#include "dac.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "dac_console";

static const char* registered_commands[] = {
  "info", "set", "voltage", "readback", "calibrate"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: help
static int cmd_help(int argc, char **argv) {
  (void)argc;
  (void)argv;
  ESP_LOGI(TAG, "DAC commands:");
  ESP_LOGI(TAG, "  help       - Show this help");
  ESP_LOGI(TAG, "  info       - Show DAC status (value, voltage, range, VREF)");
  ESP_LOGI(TAG, "  set <val>  - Set DAC code (0-4095), volatile");
  ESP_LOGI(TAG, "  voltage <v> - Set DAC by voltage (0.0 to VREF), volatile");
  ESP_LOGI(TAG, "  readback   - Read DAC register and EEPROM values");
  ESP_LOGI(TAG, "  calibrate  - Run VREF calibration now");
  return 0;
}

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

// Command: voltage - set DAC by voltage (volatile, no EEPROM)
static struct {
  struct arg_dbl *voltage;
  struct arg_end *end;
} voltage_args;

static int cmd_voltage(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &voltage_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, voltage_args.end, argv[0]);
    return 1;
  }
  
  float voltage = (float)voltage_args.voltage->dval[0];
  float vref = dac_get_vref();
  
  if (voltage < 0.0f || voltage > vref) {
    ESP_LOGE(TAG, "Voltage must be 0.0 to %.3f (current VREF)", vref);
    return 1;
  }
  
  uint16_t dac_value = dac_voltage_to_value(voltage, vref);
  esp_err_t ret = dac_set_value(dac_value);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "DAC set to %u for %.4fV (VREF=%.3fV)", (unsigned)dac_value, voltage, vref);
  } else {
    ESP_LOGE(TAG, "Failed to set DAC: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: readback
static int cmd_readback(int argc, char **argv) {
  esp_err_t ret = dac_debug_readback();
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: calibrate - run VREF calibration now
static int cmd_calibrate(int argc, char **argv) {
  (void)argc;
  (void)argv;
  esp_err_t ret = dac_calibrate_vref();
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t dac_console_init(void) {
  ESP_LOGI(TAG, "Registering dac commands");
  
  // help command
  const esp_console_cmd_t help_cmd = {
    .command = "help",
    .help = "Show available DAC commands",
    .hint = NULL,
    .func = &cmd_help,
  };
  esp_console_cmd_register(&help_cmd);
  
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
  
  // voltage command
  voltage_args.voltage = arg_dbl1(NULL, NULL, "<volts>", "Target voltage (0.0 to VREF)");
  voltage_args.end = arg_end(2);
  
  const esp_console_cmd_t voltage_cmd = {
    .command = "voltage",
    .help = "Set DAC output voltage (volatile, no EEPROM write)",
    .hint = NULL,
    .func = &cmd_voltage,
    .argtable = &voltage_args
  };
  esp_console_cmd_register(&voltage_cmd);
  
  // readback command
  const esp_console_cmd_t readback_cmd = {
    .command = "readback",
    .help = "Read back DAC register and EEPROM values",
    .hint = NULL,
    .func = &cmd_readback,
  };
  esp_console_cmd_register(&readback_cmd);
  
  // calibrate command
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Run VREF calibration now",
    .hint = NULL,
    .func = &cmd_calibrate,
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  return ESP_OK;
}

void dac_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering dac commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

