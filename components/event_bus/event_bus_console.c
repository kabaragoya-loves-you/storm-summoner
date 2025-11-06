#include "event_bus_console.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "event_bus_console";

static const char* registered_commands[] = {
  "stats", "profile_start", "profile_stop", "profile_report", "profile_reset"
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

// Command: profile_start
static int cmd_profile_start(int argc, char **argv) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_start();
  ESP_LOGI(TAG, "Event profiling started - use 'profile_report' to see results");
  #else
  ESP_LOGE(TAG, "Profiling not enabled in build");
  #endif
  return 0;
}

// Command: profile_stop
static int cmd_profile_stop(int argc, char **argv) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_stop();
  ESP_LOGI(TAG, "Event profiling stopped");
  #else
  ESP_LOGE(TAG, "Profiling not enabled in build");
  #endif
  return 0;
}

// Command: profile_report
static int cmd_profile_report(int argc, char **argv) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_report();
  #else
  ESP_LOGE(TAG, "Profiling not enabled in build");
  #endif
  return 0;
}

// Command: profile_reset
static int cmd_profile_reset(int argc, char **argv) {
  #if EVENT_BUS_ENABLE_PROFILING
  event_bus_profiling_reset();
  ESP_LOGI(TAG, "Event profiling reset");
  #else
  ESP_LOGE(TAG, "Profiling not enabled in build");
  #endif
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
  
  // profile_start command
  const esp_console_cmd_t profile_start_cmd = {
    .command = "profile_start",
    .help = "Start event profiling",
    .hint = NULL,
    .func = &cmd_profile_start,
  };
  esp_console_cmd_register(&profile_start_cmd);
  
  // profile_stop command
  const esp_console_cmd_t profile_stop_cmd = {
    .command = "profile_stop",
    .help = "Stop event profiling",
    .hint = NULL,
    .func = &cmd_profile_stop,
  };
  esp_console_cmd_register(&profile_stop_cmd);
  
  // profile_report command
  const esp_console_cmd_t profile_report_cmd = {
    .command = "profile_report",
    .help = "Show profiling report (sorted by frequency)",
    .hint = NULL,
    .func = &cmd_profile_report,
  };
  esp_console_cmd_register(&profile_report_cmd);
  
  // profile_reset command
  const esp_console_cmd_t profile_reset_cmd = {
    .command = "profile_reset",
    .help = "Reset profiling counters",
    .hint = NULL,
    .func = &cmd_profile_reset,
  };
  esp_console_cmd_register(&profile_reset_cmd);
  
  return ESP_OK;
}

void event_bus_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering event_bus commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

