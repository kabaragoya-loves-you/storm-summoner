#include "revision_console.h"
#include "revision.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "revision_console";

static const char* registered_commands[] = {
  "info", "raw"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  hw_revision_t rev = revision_get();
  const char* rev_str = revision_get_string();
  
  ESP_LOGI(TAG, "====== HARDWARE REVISION ======");
  ESP_LOGI(TAG, "Revision: %s (value: %d)", rev_str, rev);
  ESP_LOGI(TAG, "===============================");
  
  return 0;
}

// Command: raw
static int cmd_raw(int argc, char **argv) {
  uint16_t raw_adc = revision_get_raw_adc();
  
  ESP_LOGI(TAG, "====== RAW ADC VALUE ======");
  ESP_LOGI(TAG, "Raw ADC: %u counts", (unsigned)raw_adc);
  ESP_LOGI(TAG, "Detected revision: %s", revision_get_string());
  ESP_LOGI(TAG, "===========================");
  
  return 0;
}

esp_err_t revision_console_init(void) {
  ESP_LOGI(TAG, "Registering revision commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show hardware revision",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // raw command
  const esp_console_cmd_t raw_cmd = {
    .command = "raw",
    .help = "Show raw ADC value",
    .hint = NULL,
    .func = &cmd_raw,
  };
  esp_console_cmd_register(&raw_cmd);
  
  return ESP_OK;
}

void revision_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering revision commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

