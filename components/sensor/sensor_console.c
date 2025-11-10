#include "sensor_console.h"
#include "sensor.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "sensor_console";

static const char* registered_commands[] = {
  "info", "calibrate_ps", "calibrate_als", "ps_diag", "dump_regs", "hysteresis"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t ps_raw = get_ps();
  uint16_t als_raw = get_als();
  uint16_t ps_min, ps_max, als_min, als_max;
  
  proximity_get_calibration(&ps_min, &ps_max);
  als_get_calibration(&als_min, &als_max);
  
  // Calculate MIDI values (0-127)
  float ps_scaled = ((float)ps_raw - (float)ps_min) * 127.0f / ((float)ps_max - (float)ps_min);
  if (ps_scaled < 0.0f) ps_scaled = 0.0f;
  if (ps_scaled > 127.0f) ps_scaled = 127.0f;
  uint8_t ps_midi = (uint8_t)(ps_scaled + 0.5f);
  
  float als_scaled = ((float)als_raw - (float)als_min) * 127.0f / ((float)als_max - (float)als_min);
  if (als_scaled < 0.0f) als_scaled = 0.0f;
  if (als_scaled > 127.0f) als_scaled = 127.0f;
  uint8_t als_midi = (uint8_t)(als_scaled + 0.5f);
  
  ESP_LOGI(TAG, "========== SENSOR ==========");
  ESP_LOGI(TAG, "Proximity:");
  ESP_LOGI(TAG, "  Raw:  %u  (range: %u-%u)", (unsigned)ps_raw, (unsigned)ps_min, (unsigned)ps_max);
  ESP_LOGI(TAG, "  MIDI: %u  (deadzone: %u)", (unsigned)ps_midi, (unsigned)proximity_get_deadzone());
  ESP_LOGI(TAG, "  Hysteresis: %s", proximity_get_hysteresis_enabled() ? "ON" : "OFF");
  if (proximity_get_hysteresis_enabled()) {
    ESP_LOGI(TAG, "    Rest position: %u, Timeout: %u, Return speed: %u", 
      (unsigned)proximity_get_rest_position(),
      (unsigned)proximity_get_timeout(),
      (unsigned)proximity_get_return_speed());
  }
  ESP_LOGI(TAG, "Ambient Light:");
  ESP_LOGI(TAG, "  Raw:  %u  (range: %u-%u)", (unsigned)als_raw, (unsigned)als_min, (unsigned)als_max);
  ESP_LOGI(TAG, "  MIDI: %u  (deadzone: %u)", (unsigned)als_midi, (unsigned)als_get_deadzone());
  ESP_LOGI(TAG, "============================");
  
  return 0;
}

// Command: calibrate_ps
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_ps_args;

static int cmd_calibrate_ps(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_ps_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_ps_args.end, argv[0]);
    return 1;
  }
  
  int duration = calibrate_ps_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating proximity for %d ms (move hand from far to near)...", duration);
  
  esp_err_t ret = proximity_auto_calibrate(duration);
  if (ret == ESP_OK) {
    uint16_t min, max;
    proximity_get_calibration(&min, &max);
    ESP_LOGI(TAG, "Proximity calibrated: %u - %u", (unsigned)min, (unsigned)max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: calibrate_als
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_als_args;

static int cmd_calibrate_als(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_als_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_als_args.end, argv[0]);
    return 1;
  }
  
  int duration = calibrate_als_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating ALS for %d ms (cover and uncover sensor)...", duration);
  
  esp_err_t ret = als_auto_calibrate(duration);
  if (ret == ESP_OK) {
    uint16_t min, max;
    als_get_calibration(&min, &max);
    ESP_LOGI(TAG, "ALS calibrated: %u - %u", (unsigned)min, (unsigned)max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: ps_diag
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} ps_diag_args;

static int cmd_ps_diag(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &ps_diag_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, ps_diag_args.end, argv[0]);
    return 1;
  }
  
  int duration = ps_diag_args.duration->ival[0];
  ESP_LOGI(TAG, "Running proximity diagnostic test for %d ms...", duration);
  
  proximity_diagnostic_test(duration);
  
  return 0;
}

// Command: dump_regs
static int cmd_dump_regs(int argc, char **argv) {
  sensor_dump_registers();
  return 0;
}

// Command: hysteresis - enable/disable proximity hysteresis
static struct {
  struct arg_str *state;
  struct arg_end *end;
} hysteresis_args;

static int cmd_hysteresis(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &hysteresis_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, hysteresis_args.end, argv[0]);
    return 1;
  }
  
  const char* state = hysteresis_args.state->sval[0];
  bool enable;
  
  if (strcmp(state, "on") == 0) {
    enable = true;
  } else if (strcmp(state, "off") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Invalid argument. Use 'on' or 'off'");
    return 1;
  }
  
  proximity_set_hysteresis_enabled(enable);
  
  ESP_LOGI(TAG, "Hysteresis: %s", enable ? "ENABLED" : "DISABLED");
  if (enable) {
    ESP_LOGI(TAG, "  Rest position: %u", (unsigned)proximity_get_rest_position());
    ESP_LOGI(TAG, "  Return speed: %u", (unsigned)proximity_get_return_speed());
    ESP_LOGI(TAG, "  Timeout: %u", (unsigned)proximity_get_timeout());
  }
  
  return 0;
}

esp_err_t sensor_console_init(void) {
  ESP_LOGI(TAG, "Registering sensor commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show sensor readings",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // calibrate_ps command
  calibrate_ps_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration in ms");
  calibrate_ps_args.end = arg_end(2);
  
  const esp_console_cmd_t calibrate_ps_cmd = {
    .command = "calibrate_ps",
    .help = "Calibrate proximity sensor",
    .hint = NULL,
    .func = &cmd_calibrate_ps,
    .argtable = &calibrate_ps_args
  };
  esp_console_cmd_register(&calibrate_ps_cmd);
  
  // calibrate_als command
  calibrate_als_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration in ms");
  calibrate_als_args.end = arg_end(2);
  
  const esp_console_cmd_t calibrate_als_cmd = {
    .command = "calibrate_als",
    .help = "Calibrate ambient light sensor",
    .hint = NULL,
    .func = &cmd_calibrate_als,
    .argtable = &calibrate_als_args
  };
  esp_console_cmd_register(&calibrate_als_cmd);
  
  // ps_diag command
  ps_diag_args.duration = arg_int1(NULL, NULL, "<ms>", "Test duration in ms");
  ps_diag_args.end = arg_end(2);
  
  const esp_console_cmd_t ps_diag_cmd = {
    .command = "ps_diag",
    .help = "Run proximity sensor diagnostic test",
    .hint = NULL,
    .func = &cmd_ps_diag,
    .argtable = &ps_diag_args
  };
  esp_console_cmd_register(&ps_diag_cmd);
  
  // dump_regs command
  const esp_console_cmd_t dump_regs_cmd = {
    .command = "dump_regs",
    .help = "Dump VCNL4040 registers",
    .hint = NULL,
    .func = &cmd_dump_regs,
  };
  esp_console_cmd_register(&dump_regs_cmd);
  
  // hysteresis command
  hysteresis_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable or disable hysteresis");
  hysteresis_args.end = arg_end(2);
  
  const esp_console_cmd_t hysteresis_cmd = {
    .command = "hysteresis",
    .help = "Enable/disable proximity hysteresis",
    .hint = NULL,
    .func = &cmd_hysteresis,
    .argtable = &hysteresis_args
  };
  esp_console_cmd_register(&hysteresis_cmd);
  
  return ESP_OK;
}

void sensor_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering sensor commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

