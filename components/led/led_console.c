#include "led_console.h"
#include "led.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "led_console";

static const char* registered_commands[] = {
  "on", "off", "flash", "flicker_start", "flicker_stop", "enable", 
  "mode", "sundial", "info"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: on
static int cmd_on(int argc, char **argv) {
  ESP_LOGI(TAG, "LED on");
  led_set_on();
  return 0;
}

// Command: off
static int cmd_off(int argc, char **argv) {
  ESP_LOGI(TAG, "LED off");
  led_set_off();
  return 0;
}

// Command: flash
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} flash_args;

static int cmd_flash(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &flash_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, flash_args.end, argv[0]);
    return 1;
  }
  
  int duration = flash_args.duration->ival[0];
  ESP_LOGI(TAG, "Flashing LED for %d ms", duration);
  flash_led((uint32_t)duration);
  
  return 0;
}

// Command: flicker_start
static int cmd_flicker_start(int argc, char **argv) {
  ESP_LOGI(TAG, "Starting LED flicker");
  flicker_start();
  return 0;
}

// Command: flicker_stop
static int cmd_flicker_stop(int argc, char **argv) {
  ESP_LOGI(TAG, "Stopping LED flicker");
  flicker_stop();
  return 0;
}

// Command: enable
static struct {
  struct arg_str *state;
  struct arg_end *end;
} enable_args;

static int cmd_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, enable_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = enable_args.state->sval[0];
  bool enable;
  
  if (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0) {
    enable = true;
  } else if (strcmp(state_str, "off") == 0 || strcmp(state_str, "0") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  led_set_enabled(enable);
  ESP_LOGI(TAG, "LED %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: info
static int cmd_info(int argc, char **argv) {
  bool enabled = led_get_enabled();
  led_mode_t mode = led_get_mode();
  bool sundial = led_get_sundial_mode();
  bool flicker_running = flicker_is_running();
  
  ESP_LOGI(TAG, "====== LED STATUS ======");
  ESP_LOGI(TAG, "Enabled: %s", enabled ? "yes" : "no");
  ESP_LOGI(TAG, "Mode: %s", mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime");
  ESP_LOGI(TAG, "Sundial mode: %s", sundial ? "yes" : "no");
  ESP_LOGI(TAG, "Flicker: %s", flicker_running ? "running" : "stopped");
  ESP_LOGI(TAG, "Note: Tempo sync controlled via 'tempo' context");
  ESP_LOGI(TAG, "========================");
  
  return 0;
}

// Command: mode
static struct {
  struct arg_str *mode_name;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = mode_args.mode_name->sval[0];
  
  if (strcmp(mode_str, "daylight") == 0 || strcmp(mode_str, "day") == 0) {
    led_set_mode(LED_MODE_DAYLIGHT);
    ESP_LOGI(TAG, "LED mode: Daylight (off by default)");
  } else if (strcmp(mode_str, "nighttime") == 0 || strcmp(mode_str, "night") == 0) {
    led_set_mode(LED_MODE_NIGHTTIME);
    ESP_LOGI(TAG, "LED mode: Nighttime (on by default, inverted)");
  } else {
    ESP_LOGE(TAG, "Unknown mode: %s (use 'daylight' or 'nighttime')", mode_str);
    return 1;
  }
  
  return 0;
}

// Command: sundial
static struct {
  struct arg_str *state;
  struct arg_end *end;
} sundial_args;

static int cmd_sundial(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &sundial_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, sundial_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = sundial_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  led_set_sundial_mode(enable);
  ESP_LOGI(TAG, "Sundial mode: %s (auto day/night based on ambient light)", enable ? "enabled" : "disabled");
  
  return 0;
}

esp_err_t led_console_init(void) {
  ESP_LOGI(TAG, "Registering led commands");
  
  // on command
  const esp_console_cmd_t on_cmd = {
    .command = "on",
    .help = "Turn LED on",
    .hint = NULL,
    .func = &cmd_on,
  };
  esp_console_cmd_register(&on_cmd);
  
  // off command
  const esp_console_cmd_t off_cmd = {
    .command = "off",
    .help = "Turn LED off",
    .hint = NULL,
    .func = &cmd_off,
  };
  esp_console_cmd_register(&off_cmd);
  
  // flash command
  flash_args.duration = arg_int1(NULL, NULL, "<ms>", "Flash duration in ms");
  flash_args.end = arg_end(2);
  
  const esp_console_cmd_t flash_cmd = {
    .command = "flash",
    .help = "Flash LED",
    .hint = NULL,
    .func = &cmd_flash,
    .argtable = &flash_args
  };
  esp_console_cmd_register(&flash_cmd);
  
  // flicker_start command
  const esp_console_cmd_t flicker_start_cmd = {
    .command = "flicker_start",
    .help = "Start LED flicker effect",
    .hint = NULL,
    .func = &cmd_flicker_start,
  };
  esp_console_cmd_register(&flicker_start_cmd);
  
  // flicker_stop command
  const esp_console_cmd_t flicker_stop_cmd = {
    .command = "flicker_stop",
    .help = "Stop LED flicker effect",
    .hint = NULL,
    .func = &cmd_flicker_stop,
  };
  esp_console_cmd_register(&flicker_stop_cmd);
  
  // enable command
  enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  enable_args.end = arg_end(2);
  
  const esp_console_cmd_t enable_cmd = {
    .command = "enable",
    .help = "Enable/disable LED",
    .hint = NULL,
    .func = &cmd_enable,
    .argtable = &enable_args
  };
  esp_console_cmd_register(&enable_cmd);
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show LED status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // mode command
  mode_args.mode_name = arg_str1(NULL, NULL, "<daylight|nighttime>", "LED mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set LED mode (daylight/nighttime)",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // sundial command
  sundial_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  sundial_args.end = arg_end(2);
  
  const esp_console_cmd_t sundial_cmd = {
    .command = "sundial",
    .help = "Auto day/night mode based on ambient light",
    .hint = NULL,
    .func = &cmd_sundial,
    .argtable = &sundial_args
  };
  esp_console_cmd_register(&sundial_cmd);
  
  return ESP_OK;
}

void led_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering led commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

