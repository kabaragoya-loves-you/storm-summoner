#include "screensaver_console.h"
#include "screensaver.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "screensaver_console";

static const char* registered_commands[] = {
  "enable", "disable", "mode", "delay"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: enable
static int cmd_enable(int argc, char **argv) {
  ESP_LOGI(TAG, "Enabling screensaver");
  screensaver_enable();
  return 0;
}

// Command: disable
static int cmd_disable(int argc, char **argv) {
  ESP_LOGI(TAG, "Disabling screensaver");
  screensaver_disable();
  return 0;
}

// Command: mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = mode_args.mode_type->sval[0];
  screensaver_mode_t mode;
  
  if (strcmp(mode_str, "starfield") == 0) {
    mode = SCREENSAVER_MODE_STARFIELD;
  } else if (strcmp(mode_str, "elite") == 0) {
    mode = SCREENSAVER_MODE_ELITE;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: starfield or elite");
    return 1;
  }
  
  screensaver_set_mode(mode);
  ESP_LOGD(TAG, "Screensaver mode set to: %s", mode_str);
  
  return 0;
}

// Command: delay
static struct {
  struct arg_int *seconds;
  struct arg_end *end;
} delay_args;

static int cmd_delay(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &delay_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, delay_args.end, argv[0]);
    return 1;
  }
  
  int delay = delay_args.seconds->ival[0];
  screensaver_set_delay((uint16_t)delay);
  ESP_LOGD(TAG, "Screensaver delay set to %d seconds", delay);
  
  return 0;
}

esp_err_t screensaver_console_init(void) {
  ESP_LOGI(TAG, "Registering screensaver commands");
  
  // enable command
  const esp_console_cmd_t enable_cmd = {
    .command = "enable",
    .help = "Enable screensaver",
    .hint = NULL,
    .func = &cmd_enable,
  };
  esp_console_cmd_register(&enable_cmd);
  
  // disable command
  const esp_console_cmd_t disable_cmd = {
    .command = "disable",
    .help = "Disable screensaver",
    .hint = NULL,
    .func = &cmd_disable,
  };
  esp_console_cmd_register(&disable_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<starfield|elite>", "Screensaver mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set screensaver mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // delay command
  delay_args.seconds = arg_int1(NULL, NULL, "<seconds>", "Delay in seconds");
  delay_args.end = arg_end(2);
  
  const esp_console_cmd_t delay_cmd = {
    .command = "delay",
    .help = "Set screensaver delay",
    .hint = NULL,
    .func = &cmd_delay,
    .argtable = &delay_args
  };
  esp_console_cmd_register(&delay_cmd);
  
  return ESP_OK;
}

void screensaver_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering screensaver commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

