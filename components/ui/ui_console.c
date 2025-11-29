#include "ui_console.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_console.h"
#include <stdlib.h>

static const char* TAG = "ui_console";

static const char* registered_commands[] = {
  "info", "size", "top", "left"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  const char* mode_str;
  switch (g_app_mode) {
    case APP_MODE_PERFORMANCE: mode_str = "Performance"; break;
    case APP_MODE_PROGRAMMING: mode_str = "Programming"; break;
    case APP_MODE_SCREENSAVER: mode_str = "Screensaver"; break;
    default: mode_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== UI ======");
  ESP_LOGI(TAG, "App mode: %s", mode_str);
  ESP_LOGI(TAG, "Circle: size=%ld, left=%ld, top=%ld",
    boundary_circle_get_size(), boundary_circle_get_left(), boundary_circle_get_top());
  ESP_LOGI(TAG, "================");
  
  return 0;
}

// Command: size <n>
static int cmd_size(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "size: %ld", boundary_circle_get_size());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_size(val);
  ESP_LOGI(TAG, "size: %ld", val);
  return 0;
}

// Command: top <n>
static int cmd_top(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "top: %ld", boundary_circle_get_top());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_top(val);
  ESP_LOGI(TAG, "top: %ld", val);
  return 0;
}

// Command: left <n>
static int cmd_left(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "left: %ld", boundary_circle_get_left());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_left(val);
  ESP_LOGI(TAG, "left: %ld", val);
  return 0;
}

esp_err_t ui_console_init(void) {
  ESP_LOGI(TAG, "Registering ui commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show UI state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // size command
  const esp_console_cmd_t size_cmd = {
    .command = "size",
    .help = "Get/set boundary circle diameter",
    .hint = "[n]",
    .func = &cmd_size,
  };
  esp_console_cmd_register(&size_cmd);
  
  // top command
  const esp_console_cmd_t top_cmd = {
    .command = "top",
    .help = "Get/set boundary circle center Y",
    .hint = "[n]",
    .func = &cmd_top,
  };
  esp_console_cmd_register(&top_cmd);
  
  // left command
  const esp_console_cmd_t left_cmd = {
    .command = "left",
    .help = "Get/set boundary circle center X",
    .hint = "[n]",
    .func = &cmd_left,
  };
  esp_console_cmd_register(&left_cmd);
  
  return ESP_OK;
}

void ui_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering ui commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

