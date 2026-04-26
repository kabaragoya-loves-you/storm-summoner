#include "tilt_console.h"
#include "tilt.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "tilt_console";

static const char* registered_commands[] = {
  "tilt_info", "tilt_calibrate", "tilt_forgive", "tilt_deadzone", "tilt_enable"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

static int cmd_tilt_info(int argc, char **argv) {
  (void)argc; (void)argv;
  int16_t rx = tilt_get_raw(TILT_AXIS_X);
  int16_t ry = tilt_get_raw(TILT_AXIS_Y);
  uint8_t mx = tilt_get_midi(TILT_AXIS_X);
  uint8_t my = tilt_get_midi(TILT_AXIS_Y);
  ESP_LOGI(TAG, "====== TILT ======");
  ESP_LOGI(TAG, "Calibrated: %d", (int)tilt_is_calibrated());
  ESP_LOGI(TAG, "Axis X: enabled=%d raw=%d midi=%u",
    (int)tilt_axis_get_enabled(TILT_AXIS_X), (int)rx, (unsigned)mx);
  ESP_LOGI(TAG, "Axis Y: enabled=%d raw=%d midi=%u",
    (int)tilt_axis_get_enabled(TILT_AXIS_Y), (int)ry, (unsigned)my);
  ESP_LOGI(TAG, "Forgive: %d (width=%u)",
    (int)tilt_get_forgive_middle(), (unsigned)tilt_get_middle_width());
  ESP_LOGI(TAG, "Deadzone: %u  Rate: %u Hz",
    (unsigned)tilt_get_deadzone(), (unsigned)tilt_get_rate_hz());
  ESP_LOGI(TAG, "==================");
  return 0;
}

static int cmd_tilt_calibrate(int argc, char **argv) {
  (void)argc; (void)argv;
  ESP_LOGI(TAG, "Starting 5-step calibration. Hold device still between prompts.");
  tilt_cal_begin();
  const char* labels[TILT_CAL_NUM_STEPS] = {
    "FLAT / at rest (center)",
    "Fully LEFT (-X)",
    "Fully RIGHT (+X)",
    "Fully FORWARD (-Y)",
    "Fully BACK (+Y)",
  };
  for (int i = 0; i < TILT_CAL_NUM_STEPS; i++) {
    ESP_LOGI(TAG, "[%d/%d] Position: %s. Capturing in 3s...", i + 1, TILT_CAL_NUM_STEPS, labels[i]);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_err_t ret = tilt_cal_capture((tilt_cal_step_t)i);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Capture failed: %s. Aborting.", esp_err_to_name(ret));
      tilt_cal_abort();
      return 1;
    }
  }
  esp_err_t ret = tilt_cal_commit();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Commit failed (insufficient swing). Try again.");
    return 1;
  }
  ESP_LOGI(TAG, "Calibration complete.");
  return 0;
}

static struct {
  struct arg_str *state;
  struct arg_end *end;
} forgive_args;

static int cmd_tilt_forgive(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&forgive_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, forgive_args.end, argv[0]);
    return 1;
  }
  const char* s = forgive_args.state->sval[0];
  bool on = (strcmp(s, "on") == 0 || strcmp(s, "1") == 0 || strcmp(s, "true") == 0);
  tilt_set_forgive_middle(on);
  ESP_LOGI(TAG, "Forgive middle = %d", (int)on);
  return 0;
}

static struct {
  struct arg_int *value;
  struct arg_end *end;
} deadzone_args;

static int cmd_tilt_deadzone(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&deadzone_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, deadzone_args.end, argv[0]);
    return 1;
  }
  int val = deadzone_args.value->ival[0];
  if (val < 0 || val > 10) {
    ESP_LOGE(TAG, "Deadzone must be 0-10");
    return 1;
  }
  tilt_set_deadzone((uint8_t)val);
  ESP_LOGI(TAG, "Deadzone = %d", val);
  return 0;
}

static struct {
  struct arg_str *mode;
  struct arg_end *end;
} enable_args;

static int cmd_tilt_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, enable_args.end, argv[0]);
    return 1;
  }
  const char* m = enable_args.mode->sval[0];
  bool x = false, y = false;
  if (strcmp(m, "x") == 0) x = true;
  else if (strcmp(m, "y") == 0) y = true;
  else if (strcmp(m, "both") == 0) { x = true; y = true; }
  else if (strcmp(m, "none") == 0) { x = false; y = false; }
  else {
    ESP_LOGE(TAG, "Mode must be x|y|both|none");
    return 1;
  }
  tilt_axis_set_enabled(TILT_AXIS_X, x);
  tilt_axis_set_enabled(TILT_AXIS_Y, y);
  ESP_LOGI(TAG, "Tilt enable X=%d Y=%d", (int)x, (int)y);
  return 0;
}

esp_err_t tilt_console_init(void) {
  ESP_LOGI(TAG, "Registering tilt commands");

  const esp_console_cmd_t info_cmd = {
    .command = "tilt_info",
    .help = "Show tilt state",
    .hint = NULL,
    .func = &cmd_tilt_info,
  };
  esp_console_cmd_register(&info_cmd);

  const esp_console_cmd_t cal_cmd = {
    .command = "tilt_calibrate",
    .help = "Run 5-step tilt calibration wizard",
    .hint = NULL,
    .func = &cmd_tilt_calibrate,
  };
  esp_console_cmd_register(&cal_cmd);

  forgive_args.state = arg_str1(NULL, NULL, "<on|off>", "Middle-forgiveness state");
  forgive_args.end = arg_end(2);
  const esp_console_cmd_t forgive_cmd = {
    .command = "tilt_forgive",
    .help = "Enable/disable middle-forgiveness",
    .hint = NULL,
    .func = &cmd_tilt_forgive,
    .argtable = &forgive_args,
  };
  esp_console_cmd_register(&forgive_cmd);

  deadzone_args.value = arg_int1(NULL, NULL, "<0-10>", "Deadzone in MIDI units");
  deadzone_args.end = arg_end(2);
  const esp_console_cmd_t dz_cmd = {
    .command = "tilt_deadzone",
    .help = "Set tilt deadzone",
    .hint = NULL,
    .func = &cmd_tilt_deadzone,
    .argtable = &deadzone_args,
  };
  esp_console_cmd_register(&dz_cmd);

  enable_args.mode = arg_str1(NULL, NULL, "<x|y|both|none>", "Axes to enable");
  enable_args.end = arg_end(2);
  const esp_console_cmd_t en_cmd = {
    .command = "tilt_enable",
    .help = "Enable tilt axes",
    .hint = NULL,
    .func = &cmd_tilt_enable,
    .argtable = &enable_args,
  };
  esp_console_cmd_register(&en_cmd);

  return ESP_OK;
}

void tilt_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering tilt commands");
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
