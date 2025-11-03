#include "input_manager_console.h"
#include "input_manager.h"
#include "input_mode.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "input_mgr_console";

static const char* registered_commands[] = {
  "info", "mode", "cable", "velocity", "fixed_vel"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  input_mode_t mode = input_get_mode();
  bool cable_detect = input_get_cable_detection_enabled();
  velocity_mode_t vel_mode = input_get_velocity_mode();
  uint8_t fixed_vel = input_get_fixed_velocity();
  
  const char* mode_str;
  switch (mode) {
    case INPUT_MODE_CV: mode_str = "CV"; break;
    case INPUT_MODE_CLOCK_SYNC: mode_str = "Clock Sync"; break;
    case INPUT_MODE_AUDIO: mode_str = "Audio"; break;
    case INPUT_MODE_NOTE: mode_str = "Note"; break;
    default: mode_str = "Unknown"; break;
  }
  
  const char* vel_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" : "Gate Voltage";
  
  ESP_LOGI(TAG, "====== INPUT MANAGER ======");
  ESP_LOGI(TAG, "Input mode: %s", mode_str);
  ESP_LOGI(TAG, "Cable detection: %s", cable_detect ? "enabled" : "disabled");
  ESP_LOGI(TAG, "Velocity mode: %s", vel_str);
  ESP_LOGI(TAG, "Fixed velocity: %u", (unsigned)fixed_vel);
  ESP_LOGI(TAG, "===========================");
  
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
  input_mode_t mode;
  
  if (strcmp(mode_str, "cv") == 0) {
    mode = INPUT_MODE_CV;
  } else if (strcmp(mode_str, "clock") == 0 || strcmp(mode_str, "sync") == 0) {
    mode = INPUT_MODE_CLOCK_SYNC;
  } else if (strcmp(mode_str, "audio") == 0) {
    mode = INPUT_MODE_AUDIO;
  } else if (strcmp(mode_str, "note") == 0) {
    mode = INPUT_MODE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: cv, clock, audio, or note");
    return 1;
  }
  
  esp_err_t ret = input_set_mode(mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Input mode set to: %s", mode_str);
  } else {
    ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
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

// Command: velocity
static struct {
  struct arg_str *vel_mode;
  struct arg_end *end;
} velocity_args;

static int cmd_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, velocity_args.end, argv[0]);
    return 1;
  }
  
  const char* vel_str = velocity_args.vel_mode->sval[0];
  velocity_mode_t mode;
  
  if (strcmp(vel_str, "fixed") == 0) {
    mode = VELOCITY_MODE_FIXED;
  } else if (strcmp(vel_str, "gate") == 0 || strcmp(vel_str, "voltage") == 0) {
    mode = VELOCITY_MODE_GATE_VOLTAGE;
  } else {
    ESP_LOGE(TAG, "Unknown velocity mode. Use: fixed or gate");
    return 1;
  }
  
  input_set_velocity_mode(mode);
  ESP_LOGI(TAG, "Velocity mode: %s", (mode == VELOCITY_MODE_FIXED) ? "fixed" : "gate voltage");
  
  return 0;
}

// Command: fixed_vel
static struct {
  struct arg_int *vel_value;
  struct arg_end *end;
} fixed_vel_args;

static int cmd_fixed_vel(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &fixed_vel_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, fixed_vel_args.end, argv[0]);
    return 1;
  }
  
  int vel = fixed_vel_args.vel_value->ival[0];
  if (vel < 1 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 1-127");
    return 1;
  }
  
  input_set_fixed_velocity((uint8_t)vel);
  ESP_LOGI(TAG, "Fixed velocity: %d", vel);
  
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
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<cv|clock|audio|note>", "Input mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set input mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
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
  
  // velocity command
  velocity_args.vel_mode = arg_str1(NULL, NULL, "<fixed|gate>", "Velocity mode");
  velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t velocity_cmd = {
    .command = "velocity",
    .help = "Set velocity mode for NOTE mode",
    .hint = NULL,
    .func = &cmd_velocity,
    .argtable = &velocity_args
  };
  esp_console_cmd_register(&velocity_cmd);
  
  // fixed_vel command
  fixed_vel_args.vel_value = arg_int1(NULL, NULL, "<1-127>", "Fixed velocity value");
  fixed_vel_args.end = arg_end(2);
  
  const esp_console_cmd_t fixed_vel_cmd = {
    .command = "fixed_vel",
    .help = "Set fixed velocity value",
    .hint = NULL,
    .func = &cmd_fixed_vel,
    .argtable = &fixed_vel_args
  };
  esp_console_cmd_register(&fixed_vel_cmd);
  
  return ESP_OK;
}

void input_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering input_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

