#include "sample_hold_console.h"
#include "sample_hold.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "sh_console";

// Subcommand argument table
static struct {
  struct arg_str *subcmd;
  struct arg_str *value;
  struct arg_dbl *rate;
  struct arg_end *end;
} sh_args;

static void print_sh_info(void) {
  sample_hold_config_t config;
  sample_hold_get_config(&config);
  
  ESP_LOGI(TAG, "========== Sample+Hold Status ==========");
  ESP_LOGI(TAG, "  Enabled:     %s", config.enabled ? "YES" : "NO");
  ESP_LOGI(TAG, "  Mode:        %s", sample_hold_mode_to_string(config.mode));
  ESP_LOGI(TAG, "  Start Mode:  %s", sample_hold_start_mode_to_string(config.start_mode));
  ESP_LOGI(TAG, "  Rate Mode:   %s", sample_hold_rate_mode_to_string(config.rate_mode));
  if (config.rate_mode == SAMPLE_HOLD_RATE_MODE_FREE) {
    ESP_LOGI(TAG, "  Rate:        %.2f Hz", config.rate_hz_x100 / 100.0f);
  } else {
    ESP_LOGI(TAG, "  Sync Mult:   %.3fx", config.sync_mult_x1000 / 1000.0f);
  }
  ESP_LOGI(TAG, "  Glide:       %s", config.glide ? "ON" : "OFF");
  ESP_LOGI(TAG, "  Running:     %s", sample_hold_is_running() ? "YES" : "NO");
  ESP_LOGI(TAG, "  Value:       %d", sample_hold_get_value());
  ESP_LOGI(TAG, "=========================================");
}

static int cmd_sh(int argc, char **argv) {
  arg_parse(argc, argv, (void **) &sh_args);
  
  // Handle 'sh info' with no other args
  if (sh_args.subcmd->count == 0 || strcmp(sh_args.subcmd->sval[0], "info") == 0) {
    print_sh_info();
    return 0;
  }
  
  const char* subcmd = sh_args.subcmd->sval[0];
  
  if (strcmp(subcmd, "enable") == 0) {
    if (sh_args.value->count == 0) {
      ESP_LOGE(TAG, "Missing value (on/off)");
      return 1;
    }
    const char* val = sh_args.value->sval[0];
    bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
    sample_hold_set_enabled(enable);
    ESP_LOGI(TAG, "S+H %s", enable ? "enabled" : "disabled");
    
  } else if (strcmp(subcmd, "mode") == 0) {
    if (sh_args.value->count == 0) {
      ESP_LOGE(TAG, "Missing mode (continuous/step)");
      return 1;
    }
    sample_hold_mode_t mode = sample_hold_mode_from_string(sh_args.value->sval[0]);
    sample_hold_set_mode(mode);
    ESP_LOGI(TAG, "S+H mode: %s", sample_hold_mode_to_string(mode));
    
  } else if (strcmp(subcmd, "start") == 0) {
    if (sh_args.value->count == 0) {
      ESP_LOGE(TAG, "Missing start mode (running/paused/transport)");
      return 1;
    }
    sample_hold_start_mode_t mode = sample_hold_start_mode_from_string(sh_args.value->sval[0]);
    sample_hold_set_start_mode(mode);
    ESP_LOGI(TAG, "S+H start mode: %s", sample_hold_start_mode_to_string(mode));
    
  } else if (strcmp(subcmd, "rate") == 0) {
    if (sh_args.rate->count == 0) {
      ESP_LOGE(TAG, "Missing rate in Hz (0.5 - 25.0)");
      return 1;
    }
    float hz = (float)sh_args.rate->dval[0];
    sample_hold_set_rate_hz(hz);
    sample_hold_set_rate_mode(SAMPLE_HOLD_RATE_MODE_FREE);
    ESP_LOGI(TAG, "S+H rate: %.2f Hz (free mode)", sample_hold_get_rate_hz());
    
  } else if (strcmp(subcmd, "mult") == 0) {
    if (sh_args.rate->count == 0) {
      ESP_LOGE(TAG, "Missing multiplier (0.125 - 8.0)");
      return 1;
    }
    float mult = (float)sh_args.rate->dval[0];
    sample_hold_set_sync_mult(mult);
    sample_hold_set_rate_mode(SAMPLE_HOLD_RATE_MODE_SYNC);
    ESP_LOGI(TAG, "S+H multiplier: %.3fx (sync mode)", sample_hold_get_sync_mult());
    
  } else if (strcmp(subcmd, "step") == 0) {
    sample_hold_step();
    ESP_LOGI(TAG, "S+H step triggered, value: %d", sample_hold_get_value());
    
  } else if (strcmp(subcmd, "toggle") == 0) {
    sample_hold_toggle();
    ESP_LOGI(TAG, "S+H toggled: %s", sample_hold_is_running() ? "running" : "stopped");
    
  } else if (strcmp(subcmd, "glide") == 0) {
    if (sh_args.value->count == 0) {
      ESP_LOGE(TAG, "Missing value (on/off)");
      return 1;
    }
    const char* val = sh_args.value->sval[0];
    bool enable = (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0);
    sample_hold_set_glide(enable);
    ESP_LOGI(TAG, "S+H glide: %s", enable ? "on" : "off");
    
  } else {
    ESP_LOGE(TAG, "Unknown subcommand: %s", subcmd);
    ESP_LOGI(TAG, "Usage: sh <info|enable|mode|start|rate|mult|step|toggle|glide> [value]");
    return 1;
  }
  
  return 0;
}

void sample_hold_console_init(void) {
  sh_args.subcmd = arg_str0(NULL, NULL, "<cmd>", "Subcommand: info, enable, mode, start, rate, mult, step, toggle, glide");
  sh_args.value = arg_str0(NULL, NULL, "<value>", "Value for subcommand");
  sh_args.rate = arg_dbl0(NULL, NULL, "<rate>", "Rate in Hz or multiplier");
  sh_args.end = arg_end(2);
  
  const esp_console_cmd_t cmd = {
    .command = "sh",
    .help = "Sample+Hold control: sh <info|enable|mode|start|rate|mult|step|toggle|glide> [value]",
    .hint = NULL,
    .func = &cmd_sh,
    .argtable = &sh_args
  };
  
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
  ESP_LOGI(TAG, "Registered 'sh' console command");
}

const char* const* sample_hold_console_get_commands(int* count) {
  static const char* commands[] = { "sh" };
  *count = 1;
  return commands;
}
