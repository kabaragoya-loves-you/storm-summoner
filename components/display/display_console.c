#include "display_console.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "display_console";

static const char* registered_commands[] = {
  "info"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  ESP_LOGI(TAG, "====== DISPLAY ======");
  ESP_LOGI(TAG, "Display initialized");
  ESP_LOGI(TAG, "=====================");
  
  return 0;
}

esp_err_t display_console_init(void) {
  ESP_LOGI(TAG, "Registering display commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show display status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  return ESP_OK;
}

void display_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering display commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

