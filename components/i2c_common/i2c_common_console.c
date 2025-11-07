#include "i2c_common_console.h"
#include "i2c_common.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "i2c_console";

static const char* registered_commands[] = {
  "scan", "read", "write", "debug", "stats", "stats_reset"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: scan
static int cmd_scan(int argc, char **argv) {
  ESP_LOGI(TAG, "Scanning I2C bus...");
  i2c_common_scan();
  return 0;
}

// Command: read
static struct {
  struct arg_int *addr;
  struct arg_int *reg;
  struct arg_end *end;
} read_args;

static int cmd_read(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &read_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, read_args.end, argv[0]);
    return 1;
  }
  
  int addr = read_args.addr->ival[0];
  int reg = read_args.reg->ival[0];
  
  if (addr < 0 || addr > 127) {
    ESP_LOGE(TAG, "Address must be 0-127");
    return 1;
  }
  
  if (reg < 0 || reg > 255) {
    ESP_LOGE(TAG, "Register must be 0-255");
    return 1;
  }
  
  // Create a temporary device handle for reading
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = addr,
    .scl_speed_hz = 100000,
  };
  
  i2c_master_dev_handle_t dev_handle;
  esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
    return 1;
  }
  
  uint8_t data;
  ret = i2c_common_read_reg(dev_handle, (uint8_t)reg, &data);
  
  i2c_master_bus_rm_device(dev_handle);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Read from addr 0x%02X reg 0x%02X: 0x%02X (%u)", 
             addr, reg, data, (unsigned)data);
  } else {
    ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
    return 1;
  }
  
  return 0;
}

// Command: write
static struct {
  struct arg_int *addr;
  struct arg_int *reg;
  struct arg_int *value;
  struct arg_end *end;
} write_args;

static int cmd_write(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &write_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, write_args.end, argv[0]);
    return 1;
  }
  
  int addr = write_args.addr->ival[0];
  int reg = write_args.reg->ival[0];
  int val = write_args.value->ival[0];
  
  if (addr < 0 || addr > 127) {
    ESP_LOGE(TAG, "Address must be 0-127");
    return 1;
  }
  
  if (reg < 0 || reg > 255) {
    ESP_LOGE(TAG, "Register must be 0-255");
    return 1;
  }
  
  if (val < 0 || val > 255) {
    ESP_LOGE(TAG, "Value must be 0-255");
    return 1;
  }
  
  // Create a temporary device handle for writing
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = addr,
    .scl_speed_hz = 100000,
  };
  
  i2c_master_dev_handle_t dev_handle;
  esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &dev_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
    return 1;
  }
  
  ret = i2c_common_write_reg(dev_handle, (uint8_t)reg, (uint8_t)val);
  
  i2c_master_bus_rm_device(dev_handle);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Wrote to addr 0x%02X reg 0x%02X: 0x%02X", addr, reg, val);
  } else {
    ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
    return 1;
  }
  
  return 0;
}

// Command: debug
static struct {
  struct arg_str *action;
  struct arg_end *end;
} debug_args;

static int cmd_debug(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &debug_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, debug_args.end, argv[0]);
    return 1;
  }
  
  const char *action = debug_args.action->sval[0];
  
  if (strcmp(action, "on") == 0) {
    i2c_common_debug_enable(true);
  } else if (strcmp(action, "off") == 0) {
    i2c_common_debug_enable(false);
  } else {
    ESP_LOGE(TAG, "Invalid action. Use 'on' or 'off'");
    return 1;
  }
  
  return 0;
}

// Command: stats
static int cmd_stats(int argc, char **argv) {
  i2c_common_print_stats();
  return 0;
}

// Command: stats_reset
static int cmd_stats_reset(int argc, char **argv) {
  i2c_common_reset_stats();
  return 0;
}

esp_err_t i2c_common_console_init(void) {
  ESP_LOGI(TAG, "Registering i2c commands");
  
  // scan command
  const esp_console_cmd_t scan_cmd = {
    .command = "scan",
    .help = "Scan I2C bus for devices",
    .hint = NULL,
    .func = &cmd_scan,
  };
  esp_console_cmd_register(&scan_cmd);
  
  // read command
  read_args.addr = arg_int1(NULL, NULL, "<addr>", "I2C address (0-127)");
  read_args.reg = arg_int1(NULL, NULL, "<reg>", "Register address (0-255)");
  read_args.end = arg_end(3);
  
  const esp_console_cmd_t read_cmd = {
    .command = "read",
    .help = "Read register from I2C device",
    .hint = NULL,
    .func = &cmd_read,
    .argtable = &read_args
  };
  esp_console_cmd_register(&read_cmd);
  
  // write command
  write_args.addr = arg_int1(NULL, NULL, "<addr>", "I2C address (0-127)");
  write_args.reg = arg_int1(NULL, NULL, "<reg>", "Register address (0-255)");
  write_args.value = arg_int1(NULL, NULL, "<value>", "Value to write (0-255)");
  write_args.end = arg_end(4);
  
  const esp_console_cmd_t write_cmd = {
    .command = "write",
    .help = "Write register to I2C device",
    .hint = NULL,
    .func = &cmd_write,
    .argtable = &write_args
  };
  esp_console_cmd_register(&write_cmd);
  
  // debug command
  debug_args.action = arg_str1(NULL, NULL, "<on|off>", "Enable or disable debug logging");
  debug_args.end = arg_end(2);
  
  const esp_console_cmd_t debug_cmd = {
    .command = "debug",
    .help = "Enable or disable I2C debug logging",
    .hint = NULL,
    .func = &cmd_debug,
    .argtable = &debug_args
  };
  esp_console_cmd_register(&debug_cmd);
  
  // stats command
  const esp_console_cmd_t stats_cmd = {
    .command = "stats",
    .help = "Print I2C transaction statistics",
    .hint = NULL,
    .func = &cmd_stats,
  };
  esp_console_cmd_register(&stats_cmd);
  
  // stats_reset command
  const esp_console_cmd_t stats_reset_cmd = {
    .command = "stats_reset",
    .help = "Reset I2C transaction statistics",
    .hint = NULL,
    .func = &cmd_stats_reset,
  };
  esp_console_cmd_register(&stats_reset_cmd);
  
  return ESP_OK;
}

void i2c_common_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering i2c commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

