#include "config_console.h"
#include "config.h"
#include "scene.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "config_console";

// Track registered command names for cleanup
static const char* registered_commands[] = {
  "info", "scene_mode", "change_mode", "program_wrap"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info - Show device configuration
static int cmd_config_info(int argc, char **argv) {
  scene_mode_t scene_mode = scene_get_mode();
  scene_change_mode_t change_mode = scene_get_change_mode();
  uint8_t channel = device_config_get_channel();
  uint8_t program = device_config_get_program();
  bool program_wrap = config_get_program_wrap();
  
  const char* scene_mode_str = (scene_mode == SCENE_MODE_SINGLE) ? "Single" :
                                (scene_mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  const char* change_mode_str = (change_mode == CHANGE_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  const char* program_wrap_str = program_wrap ? "On (wrap around)" : "Off (clamp at 0/127)";
  
  ESP_LOGI(TAG, "====== DEVICE CONFIG ======");
  ESP_LOGI(TAG, "MIDI channel: %d", channel);
  ESP_LOGI(TAG, "Current program: %d", program);
  ESP_LOGI(TAG, "Program wrap: %s", program_wrap_str);
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Scene mode: %s", scene_mode_str);
  ESP_LOGI(TAG, "Change mode: %s", change_mode_str);
  ESP_LOGI(TAG, "===========================");
  
  return 0;
}

// Command: scene_mode - Set scene operational mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} scene_mode_args;

static int cmd_scene_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &scene_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, scene_mode_args.end, argv[0]);
    return 1;
  }
  
  const char *mode = scene_mode_args.mode_type->sval[0];
  if (strcmp(mode, "single") == 0) {
    scene_set_mode(SCENE_MODE_SINGLE);
    ESP_LOGI(TAG, "Scene mode: Single");
  } else if (strcmp(mode, "preset") == 0) {
    scene_set_mode(SCENE_MODE_PRESET_SYNC);
    ESP_LOGI(TAG, "Scene mode: Preset Sync");
  } else if (strcmp(mode, "advanced") == 0) {
    scene_set_mode(SCENE_MODE_ADVANCED);
    ESP_LOGI(TAG, "Scene mode: Advanced");
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: single, preset, or advanced");
    return 1;
  }
  return 0;
}

// Command: change_mode - Set scene change mode
static struct {
  struct arg_str *change_type;
  struct arg_end *end;
} change_mode_args;

static int cmd_change_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &change_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, change_mode_args.end, argv[0]);
    return 1;
  }
  
  const char *mode = change_mode_args.change_type->sval[0];
  if (strcmp(mode, "immediate") == 0) {
    scene_set_change_mode(CHANGE_MODE_IMMEDIATE);
    ESP_LOGI(TAG, "Change mode: Immediate");
  } else if (strcmp(mode, "pending") == 0) {
    scene_set_change_mode(CHANGE_MODE_PENDING);
    ESP_LOGI(TAG, "Change mode: Pending");
  } else {
    ESP_LOGE(TAG, "Unknown change mode. Use: immediate or pending");
    return 1;
  }
  return 0;
}

// Command: program_wrap - Set program wrap mode
static struct {
  struct arg_str *wrap_type;
  struct arg_end *end;
} program_wrap_args;

static int cmd_program_wrap(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &program_wrap_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, program_wrap_args.end, argv[0]);
    return 1;
  }
  
  const char *mode = program_wrap_args.wrap_type->sval[0];
  if (strcmp(mode, "on") == 0) {
    config_set_program_wrap(true);
    ESP_LOGI(TAG, "Program wrap: On (127->0, 0->127)");
  } else if (strcmp(mode, "off") == 0) {
    config_set_program_wrap(false);
    ESP_LOGI(TAG, "Program wrap: Off (clamp at 0/127)");
  } else {
    ESP_LOGE(TAG, "Unknown wrap mode. Use: on or off");
    return 1;
  }
  return 0;
}

esp_err_t config_console_init(void) {
  ESP_LOGI(TAG, "Registering config commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show device configuration",
    .hint = NULL,
    .func = &cmd_config_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // scene_mode command
  scene_mode_args.mode_type = arg_str1(NULL, NULL, "<single|preset|advanced>", "Scene mode");
  scene_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t scene_mode_cmd = {
    .command = "scene_mode",
    .help = "Set scene operational mode",
    .hint = NULL,
    .func = &cmd_scene_mode,
    .argtable = &scene_mode_args
  };
  esp_console_cmd_register(&scene_mode_cmd);
  
  // change_mode command
  change_mode_args.change_type = arg_str1(NULL, NULL, "<immediate|pending>", "Change mode");
  change_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t change_mode_cmd = {
    .command = "change_mode",
    .help = "Set scene change mode",
    .hint = NULL,
    .func = &cmd_change_mode,
    .argtable = &change_mode_args
  };
  esp_console_cmd_register(&change_mode_cmd);
  
  // program_wrap command
  program_wrap_args.wrap_type = arg_str1(NULL, NULL, "<on|off>", "Wrap mode");
  program_wrap_args.end = arg_end(2);
  
  const esp_console_cmd_t program_wrap_cmd = {
    .command = "program_wrap",
    .help = "Set program number wrap mode (on=wrap around, off=clamp at 0/127)",
    .hint = NULL,
    .func = &cmd_program_wrap,
    .argtable = &program_wrap_args
  };
  esp_console_cmd_register(&program_wrap_cmd);
  
  return ESP_OK;
}

void config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
