#include "haptic_console.h"
#include "haptic.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "haptic_console";

static const char* registered_commands[] = {
  "mode", "waveform", "go", "stop"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: mode
static struct {
  struct arg_int *mode_val;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  int mode = mode_args.mode_val->ival[0];
  if (mode < 0 || mode > 7) {
    ESP_LOGE(TAG, "Mode must be 0-7");
    return 1;
  }
  
  esp_err_t ret = haptic_set_mode((uint8_t)mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Haptic mode set to %d", mode);
  } else {
    ESP_LOGE(TAG, "Failed to set mode");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: waveform
static struct {
  struct arg_int *slot;
  struct arg_int *wave;
  struct arg_end *end;
} waveform_args;

static int cmd_waveform(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &waveform_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, waveform_args.end, argv[0]);
    return 1;
  }
  
  int slot = waveform_args.slot->ival[0];
  int wave = waveform_args.wave->ival[0];
  
  esp_err_t ret = haptic_set_waveform((uint8_t)slot, (uint8_t)wave);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Waveform slot %d set to %d", slot, wave);
  } else {
    ESP_LOGE(TAG, "Failed to set waveform");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: go
static int cmd_go(int argc, char **argv) {
  ESP_LOGI(TAG, "Triggering haptic");
  esp_err_t ret = haptic_go();
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: stop
static int cmd_stop(int argc, char **argv) {
  ESP_LOGI(TAG, "Stopping haptic");
  esp_err_t ret = haptic_stop();
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t haptic_console_init(void) {
  ESP_LOGI(TAG, "Registering haptic commands");
  
  // mode command
  mode_args.mode_val = arg_int1(NULL, NULL, "<0-7>", "Haptic mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set haptic mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // waveform command
  waveform_args.slot = arg_int1(NULL, NULL, "<slot>", "Waveform slot");
  waveform_args.wave = arg_int1(NULL, NULL, "<wave>", "Waveform number");
  waveform_args.end = arg_end(3);
  
  const esp_console_cmd_t waveform_cmd = {
    .command = "waveform",
    .help = "Set waveform",
    .hint = NULL,
    .func = &cmd_waveform,
    .argtable = &waveform_args
  };
  esp_console_cmd_register(&waveform_cmd);
  
  // go command
  const esp_console_cmd_t go_cmd = {
    .command = "go",
    .help = "Trigger haptic",
    .hint = NULL,
    .func = &cmd_go,
  };
  esp_console_cmd_register(&go_cmd);
  
  // stop command
  const esp_console_cmd_t stop_cmd = {
    .command = "stop",
    .help = "Stop haptic",
    .hint = NULL,
    .func = &cmd_stop,
  };
  esp_console_cmd_register(&stop_cmd);
  
  return ESP_OK;
}

void haptic_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering haptic commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

