#include "tempo_console.h"
#include "tempo.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "tempo_console";

static const char* registered_commands[] = {
  "info", "bpm", "tap", "start", "stop", "led_sync", "led_downbeat", "led_ratio", "deadzone",
  "clock_output", "clock_always", "clock_no_passthrough"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t bpm = tempo_get_bpm();
  tempo_note_divider_t divider = tempo_get_note_divider();
  time_signature_t sig = tempo_get_time_signature();
  tempo_clock_standard_t std = tempo_get_clock_standard();
  tempo_clock_source_t source = tempo_get_source();
  bool led_sync = tempo_get_led_sync();
  
  const char* div_str = (divider == DIVIDER_QUARTER) ? "Quarter" :
                        (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  
  const char* std_str = (std == CLOCK_STANDARD_24PPQN) ? "24PPQN" :
                        (std == CLOCK_STANDARD_16TH_NOTE) ? "16th Note" : "Beat";
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  
  uint8_t deadzone = tempo_get_bpm_deadzone();
  clock_output_t clk_out = tempo_get_clock_output();
  bool always_send = tempo_get_clock_always_send();
  bool disable_on_pt = tempo_get_disable_clock_on_passthrough();
  
  const char* output_names[] = {"None", "USB", "UART", "Both"};
  
  ESP_LOGI(TAG, "====== TEMPO ======");
  ESP_LOGI(TAG, "BPM: %u", (unsigned)bpm);
  ESP_LOGI(TAG, "Clock source: %s (set by current scene)", source_str);
  ESP_LOGI(TAG, "Clock output: %s", output_names[clk_out]);
  ESP_LOGI(TAG, "Always send clock: %s", always_send ? "yes" : "no");
  ESP_LOGI(TAG, "Disable on passthrough: %s", disable_on_pt ? "yes" : "no");
  ESP_LOGI(TAG, "BPM deadzone: %u (0=off, 1-5=ignore ±N BPM)", (unsigned)deadzone);
  ESP_LOGI(TAG, "Divider: %s", div_str);
  ESP_LOGI(TAG, "Time signature: %u/%u", (unsigned)sig.numerator, (unsigned)sig.denominator);
  ESP_LOGI(TAG, "Clock standard: %s", std_str);
  ESP_LOGI(TAG, "LED sync: %s", led_sync ? "enabled" : "disabled");
  if (led_sync) {
    bool emphasize = tempo_get_led_emphasize_downbeat();
    uint8_t ratio = tempo_get_led_flash_ratio();
    ESP_LOGI(TAG, "  Downbeat emphasis: %s", emphasize ? "yes" : "no");
    ESP_LOGI(TAG, "  Flash ratio: %d%% of beat", ratio);
  }
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Note: Use 'clock_source' command in scene context to change source");
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

// Command: led_downbeat
static struct {
  struct arg_str *state;
  struct arg_end *end;
} led_downbeat_args;

static int cmd_led_downbeat(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_downbeat_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_downbeat_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = led_downbeat_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  tempo_set_led_emphasize_downbeat(enable);
  ESP_LOGI(TAG, "Downbeat emphasis: %s (beat 1 %s)", enable ? "enabled" : "disabled", enable ? "2x longer" : "same length");
  
  return 0;
}

// Command: led_ratio
static struct {
  struct arg_int *ratio;
  struct arg_end *end;
} led_ratio_args;

static int cmd_led_ratio(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_ratio_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_ratio_args.end, argv[0]);
    return 1;
  }
  
  int ratio = led_ratio_args.ratio->ival[0];
  if (ratio < 1 || ratio > 10) {
    ESP_LOGE(TAG, "Flash ratio must be 1-10 (percentage of beat)");
    return 1;
  }
  
  tempo_set_led_flash_ratio((uint8_t)ratio);
  ESP_LOGI(TAG, "LED flash ratio: %d%% of beat duration", ratio);
  
  return 0;
}

// Command: deadzone
static struct {
  struct arg_int *value;
  struct arg_end *end;
} deadzone_args;

static int cmd_deadzone(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &deadzone_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, deadzone_args.end, argv[0]);
    return 1;
  }
  
  int dz = deadzone_args.value->ival[0];
  if (dz < 0 || dz > 5) {
    ESP_LOGE(TAG, "Deadzone must be 0-5");
    return 1;
  }
  
  tempo_set_bpm_deadzone((uint8_t)dz);
  if (dz == 0) {
    ESP_LOGI(TAG, "BPM deadzone: disabled (tracks all changes)");
  } else {
    ESP_LOGI(TAG, "BPM deadzone: %d (ignores ±%d BPM changes)", dz, dz);
  }
  
  return 0;
}

// Command: test_sync - Inject test sync pulses
static struct {
  struct arg_int *count;
  struct arg_int *bpm;
  struct arg_end *end;
} test_sync_args;

static int cmd_test_sync(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **)&test_sync_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, test_sync_args.end, argv[0]);
    return 1;
  }
  
  int count = test_sync_args.count->ival[0];
  int bpm = test_sync_args.bpm->ival[0];
  
  if (count < 1 || count > 100) {
    ESP_LOGE(TAG, "Count must be 1-100");
    return 1;
  }
  
  if (bpm < 20 || bpm > 300) {
    ESP_LOGE(TAG, "BPM must be 20-300");
    return 1;
  }
  
  uint32_t interval_ms = 60000 / bpm;
  ESP_LOGI(TAG, "Injecting %d sync pulses at %d BPM (interval: %lu ms)", 
           count, bpm, (unsigned long)interval_ms);
  
  for (int i = 0; i < count; i++) {
    tempo_sync_pulse();
    if (i < count - 1) {
      vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
  }
  
  ESP_LOGI(TAG, "Test sync complete");
  return 0;
}

// Command: clock_output
static struct {
  struct arg_str *output;
  struct arg_end *end;
} clock_output_args;

static int cmd_clock_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_output_args.end, argv[0]);
    return 1;
  }
  
  const char* out_str = clock_output_args.output->sval[0];
  clock_output_t output;
  
  if (strcmp(out_str, "none") == 0) {
    output = CLOCK_OUTPUT_NONE;
  } else if (strcmp(out_str, "usb") == 0) {
    output = CLOCK_OUTPUT_USB;
  } else if (strcmp(out_str, "uart") == 0) {
    output = CLOCK_OUTPUT_UART;
  } else if (strcmp(out_str, "both") == 0) {
    output = CLOCK_OUTPUT_BOTH;
  } else {
    ESP_LOGE(TAG, "Unknown output. Use: none, usb, uart, or both");
    return 1;
  }
  
  tempo_set_clock_output(output);
  return 0;
}

// Command: clock_always
static struct {
  struct arg_str *state;
  struct arg_end *end;
} clock_always_args;

static int cmd_clock_always(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_always_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_always_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = clock_always_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  tempo_set_clock_always_send(enable);
  return 0;
}

// Command: clock_no_passthrough
static struct {
  struct arg_str *state;
  struct arg_end *end;
} clock_no_pt_args;

static int cmd_clock_no_passthrough(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_no_pt_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_no_pt_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = clock_no_pt_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  tempo_set_disable_clock_on_passthrough(enable);
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
  
  // led_downbeat command
  led_downbeat_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  led_downbeat_args.end = arg_end(2);
  
  const esp_console_cmd_t led_downbeat_cmd = {
    .command = "led_downbeat",
    .help = "Emphasize beat 1 (2x longer flash)",
    .hint = NULL,
    .func = &cmd_led_downbeat,
    .argtable = &led_downbeat_args
  };
  esp_console_cmd_register(&led_downbeat_cmd);
  
  // led_ratio command
  led_ratio_args.ratio = arg_int1(NULL, NULL, "<1-10>", "Flash % of beat");
  led_ratio_args.end = arg_end(2);
  
  const esp_console_cmd_t led_ratio_cmd = {
    .command = "led_ratio",
    .help = "Set flash duration (1-10% of beat)",
    .hint = NULL,
    .func = &cmd_led_ratio,
    .argtable = &led_ratio_args
  };
  esp_console_cmd_register(&led_ratio_cmd);
  
  // deadzone command
  deadzone_args.value = arg_int1(NULL, NULL, "<0-5>", "Deadzone value");
  deadzone_args.end = arg_end(2);
  
  const esp_console_cmd_t deadzone_cmd = {
    .command = "deadzone",
    .help = "Set BPM change deadzone (0=off, 1-5=ignore ±N BPM)",
    .hint = NULL,
    .func = &cmd_deadzone,
    .argtable = &deadzone_args
  };
  esp_console_cmd_register(&deadzone_cmd);
  
  // clock_output command
  clock_output_args.output = arg_str1(NULL, NULL, "<none|usb|uart|both>", "Output interface");
  clock_output_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_output_cmd = {
    .command = "clock_output",
    .help = "Set clock output interface",
    .hint = NULL,
    .func = &cmd_clock_output,
    .argtable = &clock_output_args
  };
  esp_console_cmd_register(&clock_output_cmd);
  
  // clock_always command
  clock_always_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  clock_always_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_always_cmd = {
    .command = "clock_always",
    .help = "Send clock even when transport stopped",
    .hint = NULL,
    .func = &cmd_clock_always,
    .argtable = &clock_always_args
  };
  esp_console_cmd_register(&clock_always_cmd);
  
  // clock_no_passthrough command
  clock_no_pt_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  clock_no_pt_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_no_pt_cmd = {
    .command = "clock_no_passthrough",
    .help = "Disable clock output when passthrough active",
    .hint = NULL,
    .func = &cmd_clock_no_passthrough,
    .argtable = &clock_no_pt_args
  };
  esp_console_cmd_register(&clock_no_pt_cmd);
  
  // test_sync command
  test_sync_args.count = arg_int1(NULL, NULL, "<count>", "Number of pulses");
  test_sync_args.bpm = arg_int1(NULL, NULL, "<bpm>", "Target BPM");
  test_sync_args.end = arg_end(3);
  
  const esp_console_cmd_t test_sync_cmd = {
    .command = "test_sync",
    .help = "Inject test sync pulses (count bpm)",
    .hint = NULL,
    .func = &cmd_test_sync,
    .argtable = &test_sync_args
  };
  esp_console_cmd_register(&test_sync_cmd);
  
  return ESP_OK;
}

void tempo_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering tempo commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

