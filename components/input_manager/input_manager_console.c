#include "input_manager_console.h"
#include "input_manager.h"
#include "input_mode.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "input_mgr_console";

static const char* registered_commands[] = {
  "info", "cable"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  input_mode_t mode = input_get_mode();
  bool cable_detect = input_get_cable_detection_enabled();
  
  const char* mode_str;
  switch (mode) {
    case INPUT_MODE_CV: mode_str = "CV"; break;
    case INPUT_MODE_CLOCK_SYNC: mode_str = "Clock Sync"; break;
    case INPUT_MODE_AUDIO: mode_str = "Audio"; break;
    case INPUT_MODE_NOTE: mode_str = "Note"; break;
    default: mode_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== INPUT MANAGER ======");
  ESP_LOGI(TAG, "Input mode: %s (configured per-scene)", mode_str);
  ESP_LOGI(TAG, "Cable detection: %s", cable_detect ? "enabled" : "disabled");
  ESP_LOGI(TAG, "===========================");
  
  return 0;
}

// Command: cable
static struct {
  struct arg_str *enable;
  struct arg_end *end;
} cable_args;

static int cmd_cable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cable_args.end, argv[0]);
    return 1;
  }
  
  const char* enable_str = cable_args.enable->sval[0];
  bool enable;
  
  if (strcmp(enable_str, "on") == 0 || strcmp(enable_str, "1") == 0) {
    enable = true;
  } else if (strcmp(enable_str, "off") == 0 || strcmp(enable_str, "0") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  input_set_cable_detection_enabled(enable);
  ESP_LOGI(TAG, "Cable detection: %s", enable ? "enabled" : "disabled");
  
  return 0;
}

esp_err_t input_manager_console_init(void) {
  ESP_LOGI(TAG, "Registering input_manager commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show input manager status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // cable command
  cable_args.enable = arg_str1(NULL, NULL, "<on|off>", "Enable/disable cable detection");
  cable_args.end = arg_end(2);
  
  const esp_console_cmd_t cable_cmd = {
    .command = "cable",
    .help = "Enable/disable cable detection",
    .hint = NULL,
    .func = &cmd_cable,
    .argtable = &cable_args
  };
  esp_console_cmd_register(&cable_cmd);
  
  return ESP_OK;
}

void input_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering input_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

