#include "ui_console.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "ui_console";

static const char* registered_commands[] = {
  "info"
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
  ESP_LOGI(TAG, "================");
  
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
  
  return ESP_OK;
}

void ui_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering ui commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

