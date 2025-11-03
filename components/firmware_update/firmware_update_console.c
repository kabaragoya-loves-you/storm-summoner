#include "firmware_update_console.h"
#include "firmware_update.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "fw_update_console";

static const char* registered_commands[] = {
  "info"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  firmware_update_state_t fw_state = firmware_update_get_state();
  assets_update_state_t assets_state = assets_update_get_state();
  uint8_t fw_progress = firmware_update_get_progress();
  uint8_t assets_progress = assets_update_get_progress();
  
  const char* fw_str;
  switch (fw_state) {
    case FIRMWARE_UPDATE_IDLE: fw_str = "Idle"; break;
    case FIRMWARE_UPDATE_IN_PROGRESS: fw_str = "In Progress"; break;
    case FIRMWARE_UPDATE_COMPLETE: fw_str = "Complete"; break;
    case FIRMWARE_UPDATE_ERROR: fw_str = "Error"; break;
    default: fw_str = "Unknown"; break;
  }
  
  const char* assets_str;
  switch (assets_state) {
    case ASSETS_UPDATE_IDLE: assets_str = "Idle"; break;
    case ASSETS_UPDATE_IN_PROGRESS: assets_str = "In Progress"; break;
    case ASSETS_UPDATE_COMPLETE: assets_str = "Complete"; break;
    case ASSETS_UPDATE_ERROR: assets_str = "Error"; break;
    default: assets_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== FIRMWARE UPDATE ======");
  ESP_LOGI(TAG, "Firmware state: %s (%u%%)", fw_str, (unsigned)fw_progress);
  ESP_LOGI(TAG, "Assets state: %s (%u%%)", assets_str, (unsigned)assets_progress);
  ESP_LOGI(TAG, "=============================");
  
  return 0;
}

esp_err_t firmware_update_console_init(void) {
  ESP_LOGI(TAG, "Registering firmware_update commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show update status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  return ESP_OK;
}

void firmware_update_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering firmware_update commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

