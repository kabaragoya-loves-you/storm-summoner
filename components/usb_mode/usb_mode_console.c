#include "usb_mode_console.h"
#include "usb_mode_manager.h"
#include "esp_log.h"
#include "esp_console.h"

static const char* TAG = "usb_mode_console";

static const char* registered_commands[] = {
  "info", "midi", "msc"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  usb_mode_t mode = usb_mode_get_current();
  bool ready = usb_mode_is_ready();
  
  const char* mode_str = (mode == USB_MODE_MIDI) ? "MIDI" : "MSC";
  
  ESP_LOGI(TAG, "====== USB MODE ======");
  ESP_LOGI(TAG, "Current mode: %s", mode_str);
  ESP_LOGI(TAG, "Ready: %s", ready ? "yes" : "no");
  ESP_LOGI(TAG, "======================");
  
  return 0;
}

// Command: midi
static int cmd_midi(int argc, char **argv) {
  ESP_LOGI(TAG, "Switching to MIDI mode");
  esp_err_t ret = usb_switch_to_midi();
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "USB mode set to MIDI");
  } else {
    ESP_LOGE(TAG, "Failed to switch to MIDI mode: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: msc
static int cmd_msc(int argc, char **argv) {
  ESP_LOGI(TAG, "Switching to MSC mode");
  esp_err_t ret = usb_switch_to_msc();
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "USB mode set to MSC");
  } else {
    ESP_LOGE(TAG, "Failed to switch to MSC mode: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t usb_mode_console_init(void) {
  ESP_LOGI(TAG, "Registering usb_mode commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show USB mode",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // midi command
  const esp_console_cmd_t midi_cmd = {
    .command = "midi",
    .help = "Switch to MIDI mode",
    .hint = NULL,
    .func = &cmd_midi,
  };
  esp_console_cmd_register(&midi_cmd);
  
  // msc command
  const esp_console_cmd_t msc_cmd = {
    .command = "msc",
    .help = "Switch to MSC mode",
    .hint = NULL,
    .func = &cmd_msc,
  };
  esp_console_cmd_register(&msc_cmd);
  
  return ESP_OK;
}

void usb_mode_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering usb_mode commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

