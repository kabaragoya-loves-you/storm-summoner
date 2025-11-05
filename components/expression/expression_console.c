#include "expression_console.h"
#include "expression.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "expression_console";

static const char* registered_commands[] = {
  "info", "mode", "calibrate", "cc"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint8_t midi_val = expression_get_midi_value();
  bool connected = expression_is_connected();
  expression_mode_t mode = expression_get_mode();
  int16_t min, max;
  expression_get_range(&min, &max);
  uint8_t deadzone = expression_get_deadzone();
  
  const char* mode_str = (mode == EXPRESSION_MODE_PEDAL) ? "Pedal" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "Sustain" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "Sostenuto" : "Gate";
  
  ESP_LOGI(TAG, "====== EXPRESSION ======");
  ESP_LOGI(TAG, "Mode: %s", mode_str);
  ESP_LOGI(TAG, "MIDI value: %d (0-127)", midi_val);
  ESP_LOGI(TAG, "Connected: %s", connected ? "yes" : "no");
  ESP_LOGI(TAG, "Range: %d to %d", min, max);
  ESP_LOGI(TAG, "Deadzone: %d", deadzone);
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Note: CC routing configured in scene context");
  ESP_LOGI(TAG, "  cd scene -> expr_cc <num> / expr_curve <type>");
  ESP_LOGI(TAG, "========================");
  
  return 0;
}

// Command: mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} mode_args;

static int cmd_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = mode_args.mode_type->sval[0];
  expression_mode_t mode;
  
  if (strcmp(mode_str, "pedal") == 0) {
    mode = EXPRESSION_MODE_PEDAL;
  } else if (strcmp(mode_str, "sustain") == 0) {
    mode = EXPRESSION_MODE_SUSTAIN;
  } else if (strcmp(mode_str, "sostenuto") == 0) {
    mode = EXPRESSION_MODE_SOSTENUTO;
  } else if (strcmp(mode_str, "gate") == 0) {
    mode = EXPRESSION_MODE_GATE;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: pedal, sustain, sostenuto, or gate");
    return 1;
  }
  
  esp_err_t ret = expression_set_mode(mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Expression mode set to: %s", mode_str);
  } else {
    ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: calibrate
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_args;

static int cmd_calibrate(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_args.end, argv[0]);
    return 1;
  }
  
  int duration = calibrate_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating expression for %d ms (sweep pedal from heel to toe)...", duration);
  
  esp_err_t ret = expression_auto_calibrate(duration);
  if (ret == ESP_OK) {
    int16_t min, max;
    expression_get_range(&min, &max);
    ESP_LOGI(TAG, "Expression calibrated: %d - %d", min, max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t expression_console_init(void) {
  ESP_LOGI(TAG, "Registering expression commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show expression pedal status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<pedal|sustain|sostenuto|gate>", "Expression mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set expression mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // calibrate command
  calibrate_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration in ms");
  calibrate_args.end = arg_end(2);
  
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Calibrate expression pedal",
    .hint = NULL,
    .func = &cmd_calibrate,
    .argtable = &calibrate_args
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  // cc command removed - use scene context instead
  
  return ESP_OK;
}

void expression_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering expression commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

