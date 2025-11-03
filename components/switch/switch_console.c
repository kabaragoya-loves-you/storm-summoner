#include "switch_console.h"
#include "switch.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "switch_console";

static const char* registered_commands[] = {
  "info", "set", "off"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  switch_channel_t ch = switch_get_channel();
  uint8_t mask = switch_get_channels_mask();
  
  ESP_LOGI(TAG, "====== SWITCH ======");
  
  if (ch == SWITCH_CHANNEL_NONE) {
    ESP_LOGI(TAG, "Active channel: None");
  } else {
    ESP_LOGI(TAG, "Active channel: %d", ch);
  }
  
  ESP_LOGI(TAG, "Channel mask: 0x%02X", mask);
  ESP_LOGI(TAG, "====================");
  
  return 0;
}

// Command: set
static struct {
  struct arg_int *channel;
  struct arg_end *end;
} set_args;

static int cmd_set(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_args.end, argv[0]);
    return 1;
  }
  
  int ch = set_args.channel->ival[0];
  if (ch < 0 || ch > 7) {
    ESP_LOGE(TAG, "Channel must be 0-7");
    return 1;
  }
  
  bool ret = switch_set_channel((switch_channel_t)ch);
  if (ret) {
    ESP_LOGI(TAG, "Set channel to %d", ch);
  } else {
    ESP_LOGE(TAG, "Failed to set channel");
  }
  
  return ret ? 0 : 1;
}

// Command: off
static int cmd_off(int argc, char **argv) {
  bool ret = switch_all_off();
  if (ret) {
    ESP_LOGI(TAG, "All channels off");
  } else {
    ESP_LOGE(TAG, "Failed to turn off channels");
  }
  
  return ret ? 0 : 1;
}

esp_err_t switch_console_init(void) {
  ESP_LOGI(TAG, "Registering switch commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show switch state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // set command
  set_args.channel = arg_int1(NULL, NULL, "<0-7>", "Channel to set");
  set_args.end = arg_end(2);
  
  const esp_console_cmd_t set_cmd = {
    .command = "set",
    .help = "Set active channel",
    .hint = NULL,
    .func = &cmd_set,
    .argtable = &set_args
  };
  esp_console_cmd_register(&set_cmd);
  
  // off command
  const esp_console_cmd_t off_cmd = {
    .command = "off",
    .help = "Turn off all channels",
    .hint = NULL,
    .func = &cmd_off,
  };
  esp_console_cmd_register(&off_cmd);
  
  return ESP_OK;
}

void switch_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering switch commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

