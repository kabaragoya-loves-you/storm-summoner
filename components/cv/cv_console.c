#include "cv_console.h"
#include "cv.h"
#include "input_mode.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "cv_console";

static const char* registered_commands[] = {
  "info", "mode", "range", "calibrate"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint8_t midi_val = cv_get_midi_value();
  bool connected = cv_is_cable_connected();
  cv_mode_t mode = cv_get_mode();
  cv_range_t range = cv_get_range();
  
  const char* mode_str = (mode == CV_MODE_LINEAR) ? "Linear" : "Pitch";
  const char* range_str;
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case CV_RANGE_10V: range_str = "0-10V"; break;
    case CV_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case CV_RANGE_5V: range_str = "0-5V"; break;
    case CV_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== CV ======");
  ESP_LOGI(TAG, "Mode: %s", mode_str);
  ESP_LOGI(TAG, "Range: %s", range_str);
  ESP_LOGI(TAG, "MIDI value: %u", (unsigned)midi_val);
  ESP_LOGI(TAG, "Connected: %s", connected ? "yes" : "no");
  ESP_LOGI(TAG, "================");
  
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
  cv_mode_t mode;
  
  if (strcmp(mode_str, "linear") == 0) {
    mode = CV_MODE_LINEAR;
  } else if (strcmp(mode_str, "pitch") == 0) {
    mode = CV_MODE_PITCH;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: linear or pitch");
    return 1;
  }
  
  cv_set_mode(mode);
  ESP_LOGI(TAG, "CV mode set to: %s", mode_str);
  
  return 0;
}

// Command: range
static struct {
  struct arg_str *range_type;
  struct arg_end *end;
} range_args;

static int cmd_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, range_args.end, argv[0]);
    return 1;
  }
  
  const char* range_str = range_args.range_type->sval[0];
  cv_range_t range;
  
  if (strcmp(range_str, "10v") == 0) {
    range = CV_RANGE_10V;
  } else if (strcmp(range_str, "bi10v") == 0) {
    range = CV_RANGE_BIPOLAR_10V;
  } else if (strcmp(range_str, "5v") == 0) {
    range = CV_RANGE_5V;
  } else if (strcmp(range_str, "bi5v") == 0) {
    range = CV_RANGE_BIPOLAR_5V;
  } else if (strcmp(range_str, "3v3") == 0) {
    range = CV_RANGE_3V3;
  } else {
    ESP_LOGE(TAG, "Unknown range. Use: 10v, bi10v, 5v, bi5v, or 3v3");
    return 1;
  }
  
  cv_set_range(range);
  ESP_LOGI(TAG, "CV range set to: %s", range_str);
  
  return 0;
}

// Command: calibrate
static struct {
  struct arg_str *range_type;
  struct arg_int *duration;
  struct arg_end *end;
} calibrate_args;

static int cmd_calibrate(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &calibrate_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, calibrate_args.end, argv[0]);
    return 1;
  }
  
  const char* range_str = calibrate_args.range_type->sval[0];
  cv_range_t range;
  
  if (strcmp(range_str, "10v") == 0) {
    range = CV_RANGE_10V;
  } else if (strcmp(range_str, "bi10v") == 0) {
    range = CV_RANGE_BIPOLAR_10V;
  } else if (strcmp(range_str, "5v") == 0) {
    range = CV_RANGE_5V;
  } else if (strcmp(range_str, "bi5v") == 0) {
    range = CV_RANGE_BIPOLAR_5V;
  } else if (strcmp(range_str, "3v3") == 0) {
    range = CV_RANGE_3V3;
  } else {
    ESP_LOGE(TAG, "Unknown range. Use: 10v, bi10v, 5v, bi5v, or 3v3");
    return 1;
  }
  
  int duration = calibrate_args.duration->ival[0];
  ESP_LOGI(TAG, "Calibrating CV range %s for %d ms (sweep voltage from low to high)...", 
           range_str, duration);
  
  esp_err_t ret = cv_auto_calibrate(range, duration);
  if (ret == ESP_OK) {
    int16_t min, max;
    cv_get_calibration(range, &min, &max);
    ESP_LOGI(TAG, "CV calibrated: %d - %d", min, max);
  } else {
    ESP_LOGE(TAG, "Calibration failed");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t cv_console_init(void) {
  ESP_LOGI(TAG, "Registering cv commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show CV status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<linear|pitch>", "CV mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Set CV mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // range command
  range_args.range_type = arg_str1(NULL, NULL, "<10v|bi10v|5v|bi5v|3v3>", "Voltage range");
  range_args.end = arg_end(2);
  
  const esp_console_cmd_t range_cmd = {
    .command = "range",
    .help = "Set CV voltage range",
    .hint = NULL,
    .func = &cmd_range,
    .argtable = &range_args
  };
  esp_console_cmd_register(&range_cmd);
  
  // calibrate command
  calibrate_args.range_type = arg_str1(NULL, NULL, "<range>", "Range to calibrate");
  calibrate_args.duration = arg_int1(NULL, NULL, "<ms>", "Calibration duration");
  calibrate_args.end = arg_end(3);
  
  const esp_console_cmd_t calibrate_cmd = {
    .command = "calibrate",
    .help = "Calibrate CV range",
    .hint = NULL,
    .func = &cmd_calibrate,
    .argtable = &calibrate_args
  };
  esp_console_cmd_register(&calibrate_cmd);
  
  return ESP_OK;
}

void cv_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering cv commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

