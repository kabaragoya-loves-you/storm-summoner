#include "event_bus_console.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "event_bus_console";

static const char* registered_commands[] = {
  "stats"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: stats
static int cmd_stats(int argc, char **argv) {
  ESP_LOGI(TAG, "====== EVENT BUS STATS ======");
  #if EVENT_BUS_ENABLE_STATISTICS
  ESP_LOGI(TAG, "Event bus statistics available (function not yet implemented)");
  #else
  ESP_LOGI(TAG, "Statistics not enabled");
  #endif
  ESP_LOGI(TAG, "=============================");
  
  return 0;
}

esp_err_t event_bus_console_init(void) {
  ESP_LOGI(TAG, "Registering event_bus commands");
  
  // stats command
  const esp_console_cmd_t stats_cmd = {
    .command = "stats",
    .help = "Show event bus statistics",
    .hint = NULL,
    .func = &cmd_stats,
  };
  esp_console_cmd_register(&stats_cmd);
  
  return ESP_OK;
}

void event_bus_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering event_bus commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

