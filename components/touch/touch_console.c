#include "touch_console.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "touch_console";

static const char* registered_commands[] = {
  "calibrate", "reset", "debug"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: calibrate
static int cmd_calibrate(int argc, char **argv) {
  ESP_LOGI(TAG, "Starting touch calibration...");
  force_touch_calibration();
  ESP_LOGI(TAG, "Calibration complete");
  return 0;
}

// Command: reset
static int cmd_reset(int argc, char **argv) {
  ESP_LOGI(TAG, "Resetting stuck touch pads...");
  touch_reset_stuck_pads();
  ESP_LOGI(TAG, "Touch pads reset");
  return 0;
}

// Command: debug
static int cmd_debug(int argc, char **argv) {
  ESP_LOGI(TAG, "Enabling touch debug logging");
  touch_enable_debug_logging();
  return 0;
}

esp_err_t touch_console_init(void) {
  ESP_LOGI(TAG, "Registering touch commands");
  
  // calibrate command
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Calibrate touch sensors",
    .hint = NULL,
    .func = &cmd_calibrate,
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  // reset command
  const esp_console_cmd_t reset_cmd = {
    .command = "reset",
    .help = "Reset stuck touch pads",
    .hint = NULL,
    .func = &cmd_reset,
  };
  esp_console_cmd_register(&reset_cmd);
  
  // debug command
  const esp_console_cmd_t debug_cmd = {
    .command = "debug",
    .help = "Enable debug logging",
    .hint = NULL,
    .func = &cmd_debug,
  };
  esp_console_cmd_register(&debug_cmd);
  
  return ESP_OK;
}

void touch_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering touch commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

