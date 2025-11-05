#include "tempo_console.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "tempo_console";

static const char* registered_commands[] = {
  "info", "bpm", "source", "tap", "start", "stop", "led_sync"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t bpm = tempo_get_bpm();
  tempo_note_divider_t divider = tempo_get_note_divider();
  time_signature_t sig = tempo_get_time_signature();
  tempo_clock_standard_t std = tempo_get_clock_standard();
  bool led_sync = tempo_get_led_sync();
  
  const char* div_str = (divider == DIVIDER_QUARTER) ? "Quarter" :
                        (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  
  const char* std_str = (std == CLOCK_STANDARD_24PPQN) ? "24PPQN" :
                        (std == CLOCK_STANDARD_16TH_NOTE) ? "16th Note" : "Beat";
  
  ESP_LOGI(TAG, "====== TEMPO ======");
  ESP_LOGI(TAG, "BPM: %u", (unsigned)bpm);
  ESP_LOGI(TAG, "Divider: %s", div_str);
  ESP_LOGI(TAG, "Time signature: %u/%u", (unsigned)sig.numerator, (unsigned)sig.denominator);
  ESP_LOGI(TAG, "Clock standard: %s", std_str);
  ESP_LOGI(TAG, "LED sync: %s", led_sync ? "enabled" : "disabled");
  ESP_LOGI(TAG, "===================");
  
  return 0;
}

// Command: bpm
static struct {
  struct arg_int *value;
  struct arg_end *end;
} bpm_args;

static int cmd_bpm(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &bpm_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, bpm_args.end, argv[0]);
    return 1;
  }
  
  int bpm = bpm_args.value->ival[0];
  if (bpm < 20 || bpm > 300) {
    ESP_LOGE(TAG, "BPM must be 20-300");
    return 1;
  }
  
  tempo_set_bpm((uint16_t)bpm);
  ESP_LOGI(TAG, "BPM set to %d", bpm);
  
  return 0;
}

// Command: source
static struct {
  struct arg_str *src;
  struct arg_end *end;
} source_args;

static int cmd_source(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &source_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, source_args.end, argv[0]);
    return 1;
  }
  
  const char* src_str = source_args.src->sval[0];
  tempo_clock_source_t src;
  
  if (strcmp(src_str, "internal") == 0) {
    src = CLOCK_SOURCE_INTERNAL;
  } else if (strcmp(src_str, "midi") == 0) {
    src = CLOCK_SOURCE_MIDI;
  } else if (strcmp(src_str, "sync") == 0) {
    src = CLOCK_SOURCE_SYNC;
  } else {
    ESP_LOGE(TAG, "Unknown source. Use: internal, midi, or sync");
    return 1;
  }
  
  tempo_set_source(src);
  ESP_LOGI(TAG, "Clock source set to: %s", src_str);
  
  return 0;
}

// Command: tap
static int cmd_tap(int argc, char **argv) {
  ESP_LOGI(TAG, "Tap tempo event");
  tempo_tap_event();
  return 0;
}

// Command: start
static int cmd_start(int argc, char **argv) {
  ESP_LOGI(TAG, "Starting tempo");
  tempo_start();
  return 0;
}

// Command: stop
static int cmd_stop(int argc, char **argv) {
  ESP_LOGI(TAG, "Stopping tempo");
  tempo_stop();
  return 0;
}

// Command: led_sync
static struct {
  struct arg_str *state;
  struct arg_end *end;
} led_sync_args;

static int cmd_led_sync(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_sync_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_sync_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = led_sync_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  tempo_set_led_sync(enable);
  ESP_LOGI(TAG, "LED sync: %s", enable ? "enabled (flash on beats)" : "disabled");
  
  return 0;
}

esp_err_t tempo_console_init(void) {
  ESP_LOGI(TAG, "Registering tempo commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show tempo settings",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // bpm command
  bpm_args.value = arg_int1(NULL, NULL, "<20-300>", "BPM value");
  bpm_args.end = arg_end(2);
  
  const esp_console_cmd_t bpm_cmd = {
    .command = "bpm",
    .help = "Set BPM",
    .hint = NULL,
    .func = &cmd_bpm,
    .argtable = &bpm_args
  };
  esp_console_cmd_register(&bpm_cmd);
  
  // source command
  source_args.src = arg_str1(NULL, NULL, "<internal|midi|sync>", "Clock source");
  source_args.end = arg_end(2);
  
  const esp_console_cmd_t source_cmd = {
    .command = "source",
    .help = "Set clock source",
    .hint = NULL,
    .func = &cmd_source,
    .argtable = &source_args
  };
  esp_console_cmd_register(&source_cmd);
  
  // tap command
  const esp_console_cmd_t tap_cmd = {
    .command = "tap",
    .help = "Tap tempo",
    .hint = NULL,
    .func = &cmd_tap,
  };
  esp_console_cmd_register(&tap_cmd);
  
  // start command
  const esp_console_cmd_t start_cmd = {
    .command = "start",
    .help = "Start tempo task",
    .hint = NULL,
    .func = &cmd_start,
  };
  esp_console_cmd_register(&start_cmd);
  
  // stop command
  const esp_console_cmd_t stop_cmd = {
    .command = "stop",
    .help = "Stop tempo task",
    .hint = NULL,
    .func = &cmd_stop,
  };
  esp_console_cmd_register(&stop_cmd);
  
  // led_sync command
  led_sync_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  led_sync_args.end = arg_end(2);
  
  const esp_console_cmd_t led_sync_cmd = {
    .command = "led_sync",
    .help = "Sync LED to tempo (flash on beats)",
    .hint = NULL,
    .func = &cmd_led_sync,
    .argtable = &led_sync_args
  };
  esp_console_cmd_register(&led_sync_cmd);
  
  return ESP_OK;
}

void tempo_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering tempo commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

