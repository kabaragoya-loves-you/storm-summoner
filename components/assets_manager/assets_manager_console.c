#include "assets_manager_console.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "assets_mgr_console";

static const char* registered_commands[] = {
  "info"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  ESP_LOGI(TAG, "====== ASSETS MANAGER ======");
  ESP_LOGI(TAG, "Assets manager initialized");
  ESP_LOGI(TAG, "============================");
  
  return 0;
}

esp_err_t assets_manager_console_init(void) {
  ESP_LOGI(TAG, "Registering assets_manager commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show assets status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  return ESP_OK;
}

void assets_manager_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering assets_manager commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

