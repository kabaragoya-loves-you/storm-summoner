#include "device_config_console.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "device_config_console";

static const char* registered_commands[] = {
  "info", "channel", "trs", "mode", "pedal", "custom", "save"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  const device_config_t* cfg = device_config_get();
  
  const char* mode_str = (cfg->mode == DEVICE_MODE_DATABASE) ? "Database" : "Custom";
  const char* trs_str = (cfg->trs_type == MIDI_TRS_TYPE_A) ? "Type A" : "Type B";
  
  ESP_LOGI(TAG, "====== DEVICE CONFIG ======");
  ESP_LOGI(TAG, "Mode: %s", mode_str);
  ESP_LOGI(TAG, "MIDI Channel: %d", cfg->midi_channel);
  ESP_LOGI(TAG, "TRS Type: %s", trs_str);
  
  if (cfg->mode == DEVICE_MODE_DATABASE) {
    ESP_LOGI(TAG, "Pedal: %s", cfg->pedal_slug);
  } else {
    ESP_LOGI(TAG, "Custom Name: %s", cfg->custom_name);
  }
  ESP_LOGI(TAG, "==========================");
  
  return 0;
}

// Command: channel
static struct {
  struct arg_int *channel_num;
  struct arg_end *end;
} channel_args;

static int cmd_channel(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &channel_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, channel_args.end, argv[0]);
    return 1;
  }
  
  int ch = channel_args.channel_num->ival[0];
  if (ch < 1 || ch > 16) {
    ESP_LOGE(TAG, "Channel must be 1-16");
    return 1;
  }
  
  esp_err_t ret = device_config_set_channel(ch);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "MIDI channel set to %d", ch);
  } else {
    ESP_LOGE(TAG, "Failed to set channel: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: trs
static struct {
  struct arg_str *trs_type;
  struct arg_end *end;
} trs_args;

static int cmd_trs(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &trs_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, trs_args.end, argv[0]);
    return 1;
  }
  
  const char* type_str = trs_args.trs_type->sval[0];
  midi_trs_type_t type;
  
  if (strcmp(type_str, "a") == 0 || strcmp(type_str, "A") == 0) {
    type = MIDI_TRS_TYPE_A;
  } else if (strcmp(type_str, "b") == 0 || strcmp(type_str, "B") == 0) {
    type = MIDI_TRS_TYPE_B;
  } else {
    ESP_LOGE(TAG, "Unknown TRS type. Use: a or b");
    return 1;
  }
  
  esp_err_t ret = device_config_set_trs_type(type);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "TRS type set to %s", (type == MIDI_TRS_TYPE_A) ? "Type A" : "Type B");
  } else {
    ESP_LOGE(TAG, "Failed to set TRS type: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
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
  const device_config_t* cfg = device_config_get();
  
  if (strcmp(mode_str, "database") == 0) {
    ESP_LOGI(TAG, "Mode: Database (current pedal: %s)", cfg->pedal_slug);
  } else if (strcmp(mode_str, "custom") == 0) {
    ESP_LOGI(TAG, "Mode: Custom (current name: %s)", cfg->custom_name);
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: database or custom");
    return 1;
  }
  
  return 0;
}

// Command: pedal
static struct {
  struct arg_str *pedal_slug;
  struct arg_end *end;
} pedal_args;

static int cmd_pedal(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pedal_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pedal_args.end, argv[0]);
    return 1;
  }
  
  const char* slug = pedal_args.pedal_slug->sval[0];
  esp_err_t ret = device_config_set_pedal(slug);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Pedal set to: %s", slug);
  } else {
    ESP_LOGE(TAG, "Failed to set pedal: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: custom
static struct {
  struct arg_str *custom_name;
  struct arg_end *end;
} custom_args;

static int cmd_custom(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &custom_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, custom_args.end, argv[0]);
    return 1;
  }
  
  const char* name = custom_args.custom_name->sval[0];
  esp_err_t ret = device_config_set_custom(name);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Custom device name set to: %s", name);
  } else {
    ESP_LOGE(TAG, "Failed to set custom name: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: save
static int cmd_save(int argc, char **argv) {
  esp_err_t ret = device_config_save();
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Device configuration saved to NVS");
  } else {
    ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t device_config_console_init(void) {
  ESP_LOGI(TAG, "Registering device_config commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show device configuration",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // channel command
  channel_args.channel_num = arg_int1(NULL, NULL, "<1-16>", "MIDI channel");
  channel_args.end = arg_end(2);
  
  const esp_console_cmd_t channel_cmd = {
    .command = "channel",
    .help = "Set MIDI channel",
    .hint = NULL,
    .func = &cmd_channel,
    .argtable = &channel_args
  };
  esp_console_cmd_register(&channel_cmd);
  
  // trs command
  trs_args.trs_type = arg_str1(NULL, NULL, "<a|b>", "TRS wiring type");
  trs_args.end = arg_end(2);
  
  const esp_console_cmd_t trs_cmd = {
    .command = "trs",
    .help = "Set TRS wiring type",
    .hint = NULL,
    .func = &cmd_trs,
    .argtable = &trs_args
  };
  esp_console_cmd_register(&trs_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<database|custom>", "Device mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Show device mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // pedal command
  pedal_args.pedal_slug = arg_str1(NULL, NULL, "<slug>", "Pedal slug from database");
  pedal_args.end = arg_end(2);
  
  const esp_console_cmd_t pedal_cmd = {
    .command = "pedal",
    .help = "Set pedal from database",
    .hint = NULL,
    .func = &cmd_pedal,
    .argtable = &pedal_args
  };
  esp_console_cmd_register(&pedal_cmd);
  
  // custom command
  custom_args.custom_name = arg_str1(NULL, NULL, "<name>", "Custom device name");
  custom_args.end = arg_end(2);
  
  const esp_console_cmd_t custom_cmd = {
    .command = "custom",
    .help = "Set custom device name",
    .hint = NULL,
    .func = &cmd_custom,
    .argtable = &custom_args
  };
  esp_console_cmd_register(&custom_cmd);
  
  // save command
  const esp_console_cmd_t save_cmd = {
    .command = "save",
    .help = "Save configuration to NVS",
    .hint = NULL,
    .func = &cmd_save,
  };
  esp_console_cmd_register(&save_cmd);
  
  return ESP_OK;
}

void device_config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering device_config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

