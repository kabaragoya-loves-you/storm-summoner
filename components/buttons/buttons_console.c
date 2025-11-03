#include "buttons_console.h"
#include "buttons.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "buttons_console";

static const char* registered_commands[] = {
  "info", "debounce", "chord", "longpress"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  button_state_t state = buttons_get_state();
  uint16_t debounce = buttons_get_debounce();
  uint16_t chord = buttons_get_chord_window();
  uint16_t longpress = buttons_get_long_press_threshold();
  
  ESP_LOGI(TAG, "====== BUTTONS ======");
  ESP_LOGI(TAG, "Left: %s", state.left_pressed ? "pressed" : "released");
  ESP_LOGI(TAG, "Right: %s", state.right_pressed ? "pressed" : "released");
  ESP_LOGI(TAG, "Both: %s", state.both_pressed ? "pressed" : "released");
  ESP_LOGI(TAG, "Debounce: %u ms", (unsigned)debounce);
  ESP_LOGI(TAG, "Chord window: %u ms", (unsigned)chord);
  ESP_LOGI(TAG, "Long press: %u ms", (unsigned)longpress);
  ESP_LOGI(TAG, "=====================");
  
  return 0;
}

// Command: debounce
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
  esp_err_t ret = buttons_set_debounce(val);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Debounce set to %d ms", val);
  } else {
    ESP_LOGE(TAG, "Failed to set debounce");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: chord
static struct {
  struct arg_int *ms;
  struct arg_end *end;
} chord_args;

static int cmd_chord(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &chord_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, chord_args.end, argv[0]);
    return 1;
  }
  
  int val = chord_args.ms->ival[0];
  esp_err_t ret = buttons_set_chord_window(val);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Chord window set to %d ms", val);
  } else {
    ESP_LOGE(TAG, "Failed to set chord window");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: longpress
static struct {
  struct arg_int *ms;
  struct arg_end *end;
} longpress_args;

static int cmd_longpress(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &longpress_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, longpress_args.end, argv[0]);
    return 1;
  }
  
  int val = longpress_args.ms->ival[0];
  esp_err_t ret = buttons_set_long_press_threshold(val);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Long press threshold set to %d ms", val);
  } else {
    ESP_LOGE(TAG, "Failed to set long press threshold");
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

esp_err_t buttons_console_init(void) {
  ESP_LOGI(TAG, "Registering buttons commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show button states",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // debounce command
  debounce_args.ms = arg_int1(NULL, NULL, "<ms>", "Debounce delay in ms (0-100)");
  debounce_args.end = arg_end(2);
  
  const esp_console_cmd_t debounce_cmd = {
    .command = "debounce",
    .help = "Set debounce delay",
    .hint = NULL,
    .func = &cmd_debounce,
    .argtable = &debounce_args
  };
  esp_console_cmd_register(&debounce_cmd);
  
  // chord command
  chord_args.ms = arg_int1(NULL, NULL, "<ms>", "Chord window in ms (0-500)");
  chord_args.end = arg_end(2);
  
  const esp_console_cmd_t chord_cmd = {
    .command = "chord",
    .help = "Set chord detection window",
    .hint = NULL,
    .func = &cmd_chord,
    .argtable = &chord_args
  };
  esp_console_cmd_register(&chord_cmd);
  
  // longpress command
  longpress_args.ms = arg_int1(NULL, NULL, "<ms>", "Long press threshold in ms (100-5000)");
  longpress_args.end = arg_end(2);
  
  const esp_console_cmd_t longpress_cmd = {
    .command = "longpress",
    .help = "Set long press threshold",
    .hint = NULL,
    .func = &cmd_longpress,
    .argtable = &longpress_args
  };
  esp_console_cmd_register(&longpress_cmd);
  
  return ESP_OK;
}

void buttons_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering buttons commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

