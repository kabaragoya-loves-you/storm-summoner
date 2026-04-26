#include "bump_console.h"
#include "bump.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "bump_console";

static const char* registered_commands[] = {
  "info", "threshold", "debounce", "intensity", "sensitivity"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

static int cmd_info(int argc, char **argv) {
  (void)argc; (void)argv;
  uint8_t threshold = bump_get_threshold();
  uint32_t debounce = bump_get_debounce();
  uint32_t intensity = bump_get_intensity_threshold();
  uint8_t sensitivity = bump_get_sensitivity_level();
  
  ESP_LOGI(TAG, "====== BUMP SENSOR ======");
  ESP_LOGI(TAG, "HW Threshold: %u", (unsigned)threshold);
  ESP_LOGI(TAG, "Debounce: %u ms", (unsigned)debounce);
  ESP_LOGI(TAG, "Intensity: %u mg", (unsigned)intensity);
  ESP_LOGI(TAG, "Sensitivity: %u (1-10)", (unsigned)sensitivity);
  ESP_LOGI(TAG, "=========================");
  
  return 0;
}

static struct {
  struct arg_int *value;
  struct arg_end *end;
} threshold_args;

static int cmd_threshold(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &threshold_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, threshold_args.end, argv[0]);
    return 1;
  }
  
  int val = threshold_args.value->ival[0];
  if (val < 0 || val > 127) {
    ESP_LOGE(TAG, "Threshold must be 0-127");
    return 1;
  }
  
  bump_set_threshold((uint8_t)val);
  ESP_LOGI(TAG, "HW threshold set to %d", val);
  
  return 0;
}

static struct {
  struct arg_int *ms;
  struct arg_end *end;
} debounce_args;

static int cmd_debounce(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &debounce_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, debounce_args.end, argv[0]);
    return 1;
  }
  
  int val = debounce_args.ms->ival[0];
  bump_set_debounce(val);
  ESP_LOGI(TAG, "Debounce set to %d ms", val);
  
  return 0;
}

static struct {
  struct arg_int *mg;
  struct arg_end *end;
} intensity_args;

static int cmd_intensity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &intensity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, intensity_args.end, argv[0]);
    return 1;
  }
  
  int val = intensity_args.mg->ival[0];
  bump_set_intensity_threshold(val);
  ESP_LOGI(TAG, "Intensity threshold set to %d mg", val);
  
  return 0;
}

static struct {
  struct arg_int *level;
  struct arg_end *end;
} sensitivity_args;

static int cmd_sensitivity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &sensitivity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, sensitivity_args.end, argv[0]);
    return 1;
  }
  
  int val = sensitivity_args.level->ival[0];
  if (val < 1 || val > 10) {
    ESP_LOGE(TAG, "Sensitivity must be 1-10");
    return 1;
  }
  
  bump_set_sensitivity_level((uint8_t)val);
  ESP_LOGI(TAG, "Sensitivity level set to %d", val);
  
  return 0;
}

esp_err_t bump_console_init(void) {
  ESP_LOGI(TAG, "Registering bump commands");
  
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show bump sensor status",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  threshold_args.value = arg_int1(NULL, NULL, "<0-127>", "HW threshold value");
  threshold_args.end = arg_end(2);
  const esp_console_cmd_t threshold_cmd = {
    .command = "threshold",
    .help = "Set hardware threshold",
    .hint = NULL,
    .func = &cmd_threshold,
    .argtable = &threshold_args
  };
  esp_console_cmd_register(&threshold_cmd);
  
  debounce_args.ms = arg_int1(NULL, NULL, "<ms>", "Debounce delay in ms");
  debounce_args.end = arg_end(2);
  const esp_console_cmd_t debounce_cmd = {
    .command = "debounce",
    .help = "Set debounce delay",
    .hint = NULL,
    .func = &cmd_debounce,
    .argtable = &debounce_args
  };
  esp_console_cmd_register(&debounce_cmd);
  
  intensity_args.mg = arg_int1(NULL, NULL, "<mg>", "Intensity threshold in mg");
  intensity_args.end = arg_end(2);
  const esp_console_cmd_t intensity_cmd = {
    .command = "intensity",
    .help = "Set intensity threshold",
    .hint = NULL,
    .func = &cmd_intensity,
    .argtable = &intensity_args
  };
  esp_console_cmd_register(&intensity_cmd);
  
  sensitivity_args.level = arg_int1(NULL, NULL, "<1-10>", "Sensitivity level");
  sensitivity_args.end = arg_end(2);
  const esp_console_cmd_t sensitivity_cmd = {
    .command = "sensitivity",
    .help = "Set sensitivity level (1=sensitive, 10=insensitive)",
    .hint = NULL,
    .func = &cmd_sensitivity,
    .argtable = &sensitivity_args
  };
  esp_console_cmd_register(&sensitivity_cmd);
  
  return ESP_OK;
}

void bump_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering bump commands");
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
