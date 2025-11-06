#include "cv_console.h"
#include "cv.h"
#include "input_mode.h"
#include "input_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "cv_console";

static const char* registered_commands[] = {
  "info", "cv_mode", "range", "calibrate", "input_mode", "pitch_standard", "velocity_mode", "fixed_velocity"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  bool connected = cv_is_cable_connected();
  cv_mode_t cv_mode = cv_get_mode();
  cv_range_t range = cv_get_range();
  input_mode_t input_mode = input_get_mode();
  cv_pitch_standard_t pitch_std = cv_get_pitch_standard();
  velocity_mode_t vel_mode = input_get_velocity_mode();
  uint8_t fixed_vel = input_get_fixed_velocity();
  uint8_t deadzone = cv_get_deadzone();
  
  const char* cv_mode_str = (cv_mode == CV_MODE_LINEAR) ? "Linear" : "Pitch";
  
  const char* range_str;
  switch (range) {
    case CV_RANGE_BIPOLAR_10V: range_str = "±10V"; break;
    case CV_RANGE_10V: range_str = "0-10V"; break;
    case CV_RANGE_BIPOLAR_5V: range_str = "±5V"; break;
    case CV_RANGE_5V: range_str = "0-5V"; break;
    case CV_RANGE_3V3: range_str = "0-3.3V"; break;
    default: range_str = "Unknown"; break;
  }
  
  const char* input_mode_str;
  switch (input_mode) {
    case INPUT_MODE_CV: input_mode_str = "CV"; break;
    case INPUT_MODE_CLOCK_SYNC: input_mode_str = "Clock Sync"; break;
    case INPUT_MODE_AUDIO: input_mode_str = "Audio"; break;
    case INPUT_MODE_NOTE: input_mode_str = "Note"; break;
    default: input_mode_str = "Unknown"; break;
  }
  
  const char* pitch_std_str;
  switch (pitch_std) {
    case CV_PITCH_1V_OCTAVE_C0: pitch_std_str = "1V/Oct (C0@0V)"; break;
    case CV_PITCH_1V_OCTAVE_C2: pitch_std_str = "1V/Oct (C2@0V)"; break;
    case CV_PITCH_HZ_V: pitch_std_str = "Hz/V (Buchla)"; break;
    default: pitch_std_str = "Unknown"; break;
  }
  
  const char* vel_mode_str = (vel_mode == VELOCITY_MODE_FIXED) ? "Fixed" : "Gate Voltage";
  
  ESP_LOGI(TAG, "====== CV INPUT ======");
  ESP_LOGI(TAG, "Input mode: %s", input_mode_str);
  ESP_LOGI(TAG, "CV mode: %s", cv_mode_str);
  ESP_LOGI(TAG, "Voltage range: %s", range_str);
  if (cv_mode == CV_MODE_PITCH) {
    ESP_LOGI(TAG, "Pitch standard: %s", pitch_std_str);
  }
  if (input_mode == INPUT_MODE_NOTE) {
    ESP_LOGI(TAG, "Velocity mode: %s", vel_mode_str);
    if (vel_mode == VELOCITY_MODE_FIXED) {
      ESP_LOGI(TAG, "Fixed velocity: %u", (unsigned)fixed_vel);
    }
  }
  ESP_LOGI(TAG, "Deadzone: %u", (unsigned)deadzone);
  ESP_LOGI(TAG, "Cable: %s", connected ? "connected" : "disconnected");
  ESP_LOGI(TAG, "======================");
  
  return 0;
}

// Command: cv_mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} cv_mode_args;

static int cmd_cv_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = cv_mode_args.mode_type->sval[0];
  cv_mode_t mode;
  
  if (strcmp(mode_str, "linear") == 0) {
    mode = CV_MODE_LINEAR;
  } else if (strcmp(mode_str, "pitch") == 0) {
    mode = CV_MODE_PITCH;
  } else {
    ESP_LOGE(TAG, "Unknown CV mode. Use: linear or pitch");
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

// Command: input_mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} input_mode_args;

static int cmd_input_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &input_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, input_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = input_mode_args.mode_type->sval[0];
  input_mode_t mode;
  
  if (strcmp(mode_str, "cv") == 0) {
    mode = INPUT_MODE_CV;
  } else if (strcmp(mode_str, "clock") == 0 || strcmp(mode_str, "clock_sync") == 0) {
    mode = INPUT_MODE_CLOCK_SYNC;
  } else if (strcmp(mode_str, "audio") == 0) {
    mode = INPUT_MODE_AUDIO;
  } else if (strcmp(mode_str, "note") == 0) {
    mode = INPUT_MODE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown input mode. Use: cv, clock_sync, audio, or note");
    return 1;
  }
  
  esp_err_t ret = input_set_mode(mode);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Input mode set to: %s", mode_str);
  } else {
    ESP_LOGE(TAG, "Failed to set input mode");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: pitch_standard
static struct {
  struct arg_str *standard_type;
  struct arg_end *end;
} pitch_standard_args;

static int cmd_pitch_standard(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pitch_standard_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pitch_standard_args.end, argv[0]);
    return 1;
  }
  
  const char* std_str = pitch_standard_args.standard_type->sval[0];
  cv_pitch_standard_t standard;
  
  if (strcmp(std_str, "c0") == 0 || strcmp(std_str, "1v_c0") == 0) {
    standard = CV_PITCH_1V_OCTAVE_C0;
  } else if (strcmp(std_str, "c2") == 0 || strcmp(std_str, "1v_c2") == 0) {
    standard = CV_PITCH_1V_OCTAVE_C2;
  } else if (strcmp(std_str, "hz_v") == 0 || strcmp(std_str, "buchla") == 0) {
    standard = CV_PITCH_HZ_V;
  } else {
    ESP_LOGE(TAG, "Unknown pitch standard. Use: c0, c2, or hz_v");
    return 1;
  }
  
  cv_set_pitch_standard(standard);
  ESP_LOGI(TAG, "Pitch standard set to: %s", std_str);
  
  return 0;
}

// Command: velocity_mode
static struct {
  struct arg_str *mode_type;
  struct arg_end *end;
} velocity_mode_args;

static int cmd_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = velocity_mode_args.mode_type->sval[0];
  velocity_mode_t mode;
  
  if (strcmp(mode_str, "fixed") == 0) {
    mode = VELOCITY_MODE_FIXED;
  } else if (strcmp(mode_str, "gate") == 0 || strcmp(mode_str, "gate_voltage") == 0) {
    mode = VELOCITY_MODE_GATE_VOLTAGE;
  } else {
    ESP_LOGE(TAG, "Unknown velocity mode. Use: fixed or gate");
    return 1;
  }
  
  input_set_velocity_mode(mode);
  ESP_LOGI(TAG, "Velocity mode set to: %s", mode_str);
  
  return 0;
}

// Command: fixed_velocity
static struct {
  struct arg_int *velocity_val;
  struct arg_end *end;
} fixed_velocity_args;

static int cmd_fixed_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &fixed_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, fixed_velocity_args.end, argv[0]);
    return 1;
  }
  
  int vel = fixed_velocity_args.velocity_val->ival[0];
  if (vel < 1 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 1-127");
    return 1;
  }
  
  input_set_fixed_velocity((uint8_t)vel);
  ESP_LOGI(TAG, "Fixed velocity set to: %d", vel);
  
  return 0;
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
  
  // cv_mode command
  cv_mode_args.mode_type = arg_str1(NULL, NULL, "<linear|pitch>", "CV processing mode");
  cv_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_mode_cmd = {
    .command = "cv_mode",
    .help = "Set CV processing mode (linear or pitch)",
    .hint = NULL,
    .func = &cmd_cv_mode,
    .argtable = &cv_mode_args
  };
  esp_console_cmd_register(&cv_mode_cmd);
  
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
  
  // input_mode command
  input_mode_args.mode_type = arg_str1(NULL, NULL, "<cv|clock_sync|audio|note>", "Input mode");
  input_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t input_mode_cmd = {
    .command = "input_mode",
    .help = "Set input mode (CV, clock sync, audio, or note)",
    .hint = NULL,
    .func = &cmd_input_mode,
    .argtable = &input_mode_args
  };
  esp_console_cmd_register(&input_mode_cmd);
  
  // pitch_standard command
  pitch_standard_args.standard_type = arg_str1(NULL, NULL, "<c0|c2|hz_v>", "Pitch standard");
  pitch_standard_args.end = arg_end(2);
  
  const esp_console_cmd_t pitch_standard_cmd = {
    .command = "pitch_standard",
    .help = "Set pitch CV standard (c0, c2, or hz_v)",
    .hint = NULL,
    .func = &cmd_pitch_standard,
    .argtable = &pitch_standard_args
  };
  esp_console_cmd_register(&pitch_standard_cmd);
  
  // velocity_mode command
  velocity_mode_args.mode_type = arg_str1(NULL, NULL, "<fixed|gate>", "Velocity mode");
  velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t velocity_mode_cmd = {
    .command = "velocity_mode",
    .help = "Set velocity mode for NOTE input mode",
    .hint = NULL,
    .func = &cmd_velocity_mode,
    .argtable = &velocity_mode_args
  };
  esp_console_cmd_register(&velocity_mode_cmd);
  
  // fixed_velocity command
  fixed_velocity_args.velocity_val = arg_int1(NULL, NULL, "<1-127>", "Fixed velocity value");
  fixed_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t fixed_velocity_cmd = {
    .command = "fixed_velocity",
    .help = "Set fixed velocity value (1-127)",
    .hint = NULL,
    .func = &cmd_fixed_velocity,
    .argtable = &fixed_velocity_args
  };
  esp_console_cmd_register(&fixed_velocity_cmd);
  
  return ESP_OK;
}

void cv_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering cv commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

