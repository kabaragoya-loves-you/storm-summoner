#include "switch_console.h"
#include "switch.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "switch_console";

static const char* registered_commands[] = {
  "info", "set"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint8_t mask = switch_get_channels_mask();
  uint8_t cv_mask = mask & 0x0F;
  uint8_t expr_mask = (mask >> 4) & 0x0F;
  
  ESP_LOGI(TAG, "====== SWITCH ======");
  ESP_LOGI(TAG, "Full mask: 0x%02X", mask);
  ESP_LOGI(TAG, "CV (0-3):   0x%X [0:%s 1:%s 2:%s 3:%s]",
    cv_mask,
    (cv_mask & 0x01) ? "ON" : "off",
    (cv_mask & 0x02) ? "ON" : "off",
    (cv_mask & 0x04) ? "ON" : "off",
    (cv_mask & 0x08) ? "ON" : "off");
  ESP_LOGI(TAG, "Expr (4-7): 0x%X [4:%s 5:%s 6:%s 7:%s]",
    expr_mask,
    (expr_mask & 0x01) ? "ON" : "off",
    (expr_mask & 0x02) ? "ON" : "off",
    (expr_mask & 0x04) ? "ON" : "off",
    (expr_mask & 0x08) ? "ON" : "off");
  ESP_LOGI(TAG, "====================");
  
  return 0;
}

// Command: set
// Supports:
//   set cv <0-15>       - Set CV channels 0-3 using bitmask
//   set expr <0-15>     - Set expression channels 4-7 using bitmask
//   set <0-7> <on|off>  - Toggle individual channel
static struct {
  struct arg_str *target;
  struct arg_str *value;
  struct arg_end *end;
} set_args;

static int cmd_set(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &set_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, set_args.end, argv[0]);
    return 1;
  }
  
  const char* target = set_args.target->sval[0];
  const char* value = set_args.value->count > 0 ? set_args.value->sval[0] : NULL;
  
  // Handle "set cv <mask>"
  if (strcmp(target, "cv") == 0) {
    if (!value) {
      ESP_LOGE(TAG, "Usage: set cv <0-15>");
      return 1;
    }
    int mask_val = atoi(value);
    if (mask_val < 0 || mask_val > 15) {
      ESP_LOGE(TAG, "CV mask must be 0-15");
      return 1;
    }
    bool ret = switch_set_cv_mask((uint8_t)mask_val);
    if (ret) {
      ESP_LOGI(TAG, "CV mask set to 0x%X", mask_val);
    } else {
      ESP_LOGE(TAG, "Failed to set CV mask");
    }
    return ret ? 0 : 1;
  }
  
  // Handle "set expr <mask>"
  if (strcmp(target, "expr") == 0) {
    if (!value) {
      ESP_LOGE(TAG, "Usage: set expr <0-15>");
      return 1;
    }
    int mask_val = atoi(value);
    if (mask_val < 0 || mask_val > 15) {
      ESP_LOGE(TAG, "Expression mask must be 0-15");
      return 1;
    }
    // shift to bits 4-7
    bool ret = switch_set_expression_mask((uint8_t)(mask_val << 4));
    if (ret) {
      ESP_LOGI(TAG, "Expression mask set to 0x%X", mask_val);
    } else {
      ESP_LOGE(TAG, "Failed to set expression mask");
    }
    return ret ? 0 : 1;
  }
  
  // Handle "set <channel> <on|off>"
  int channel = atoi(target);
  // Check if target is actually a number (atoi returns 0 for non-numeric)
  if (channel == 0 && target[0] != '0') {
    ESP_LOGE(TAG, "Unknown target '%s'. Use: cv, expr, or 0-7", target);
    return 1;
  }
  
  if (channel < 0 || channel > 7) {
    ESP_LOGE(TAG, "Channel must be 0-7");
    return 1;
  }
  
  if (!value) {
    ESP_LOGE(TAG, "Usage: set <0-7> <on|off>");
    return 1;
  }
  
  bool turn_on;
  if (strcmp(value, "on") == 0 || strcmp(value, "1") == 0) {
    turn_on = true;
  } else if (strcmp(value, "off") == 0 || strcmp(value, "0") == 0) {
    turn_on = false;
  } else {
    ESP_LOGE(TAG, "Value must be 'on' or 'off'");
    return 1;
  }
  
  // Get current mask and toggle the specific bit
  uint8_t current_mask = switch_get_channels_mask();
  uint8_t new_mask;
  if (turn_on) {
    new_mask = current_mask | (1 << channel);
  } else {
    new_mask = current_mask & ~(1 << channel);
  }
  
  bool ret = switch_set_channels_mask(new_mask);
  if (ret) {
    ESP_LOGI(TAG, "Channel %d set to %s (mask: 0x%02X)", channel, turn_on ? "ON" : "off", new_mask);
  } else {
    ESP_LOGE(TAG, "Failed to set channel %d", channel);
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
  set_args.target = arg_str1(NULL, NULL, "<cv|expr|0-7>", "Target: cv, expr, or channel 0-7");
  set_args.value = arg_str0(NULL, NULL, "<mask|on|off>", "Mask 0-15 for cv/expr, or on/off for channel");
  set_args.end = arg_end(3);
  
  const esp_console_cmd_t set_cmd = {
    .command = "set",
    .help = "Set switch channels: 'set cv <0-15>', 'set expr <0-15>', 'set <0-7> <on|off>'",
    .hint = NULL,
    .func = &cmd_set,
    .argtable = &set_args
  };
  esp_console_cmd_register(&set_cmd);
  
  return ESP_OK;
}

void switch_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering switch commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
