#include "tempo_console.h"
#include "tempo.h"
#include "transport.h"
#include "scene.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "tempo_console";

static const char* registered_commands[] = {
  "info", "bpm", "tap", "start", "stop",
  "led_sync", "led_ratio", "deadzone",
  "clock_output", "clock_always", "clock_no_passthrough", "clock_standard",
  // LED commands (merged from led component)
  "led_on", "led_off", "led_flash", "led_enable", "led_mode", "led_sundial", "led_info"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  uint16_t bpm = tempo_get_bpm();
  tempo_clock_source_t source = tempo_get_source();
  tempo_clock_standard_t standard = tempo_get_clock_standard();
  bool led_sync = tempo_get_led_sync();
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  
  const char* std_str = (standard == CLOCK_STANDARD_24PPQN) ? "24PPQN (DIN)" :
                        (standard == CLOCK_STANDARD_16TH_NOTE) ? "16th (Volca)" : "Beat (Modular)";
  
  uint8_t deadzone = tempo_get_bpm_deadzone();
  clock_output_t clk_out = tempo_get_clock_output();
  bool always_send = tempo_get_clock_always_send();
  bool disable_on_pt = tempo_get_disable_clock_on_passthrough();
  
  const char* output_names[] = {"None", "USB", "UART", "Both"};

  ESP_LOGI(TAG, "============= TEMPO =============");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "--- Runtime State (per-scene) ---");
  ESP_LOGI(TAG, "BPM: %u", (unsigned)bpm);
  ESP_LOGI(TAG, "Clock source: %s", source_str);
  {
    uint8_t scene_idx = scene_get_current_index();
    bool use_transport = scene_get_use_transport(scene_idx);
    uint32_t bar = use_transport ? transport_get_current_bar() : tempo_get_current_bar();
    uint8_t beat = use_transport ? transport_get_current_beat() : tempo_get_current_beat();
    ESP_LOGI(TAG, "Position: Bar %lu, Beat %u%s", (unsigned long)bar, (unsigned)beat,
      use_transport ? "" : " (free-run from scene load)");
  }
  ESP_LOGI(TAG, "(Use 'scene' context for BPM, clock source, beat divider, time signature)");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "--- Clock Output (global) ---");
  ESP_LOGI(TAG, "Output: %s", output_names[clk_out]);
  ESP_LOGI(TAG, "Standard: %s", std_str);
  ESP_LOGI(TAG, "Always send: %s", always_send ? "yes" : "no");
  ESP_LOGI(TAG, "Disable on passthrough: %s", disable_on_pt ? "yes" : "no");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "--- Stability (global) ---");
  if (deadzone == 0) {
    ESP_LOGI(TAG, "BPM deadzone: off");
  } else {
    ESP_LOGI(TAG, "BPM deadzone: %u (ignores +/-%u BPM)", (unsigned)deadzone, (unsigned)deadzone);
  }
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "--- LED Sync (global) ---");
  ESP_LOGI(TAG, "LED sync: %s", led_sync ? "enabled" : "disabled");
  if (led_sync) {
    uint8_t ratio = tempo_get_led_flash_ratio();
    ESP_LOGI(TAG, "Flash ratio: %d%% of beat", ratio);
  }
  ESP_LOGI(TAG, "=================================");
  
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
  ESP_LOGI(TAG, "BPM set to %d (use 'scene bpm' to persist)", bpm);
  
  return 0;
}

// Command: tap
static int cmd_tap(int argc, char **argv) {
  tempo_tap();
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

// Command: clock_standard
static struct {
  struct arg_str *standard;
  struct arg_end *end;
} clock_standard_args;

static int cmd_clock_standard(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_standard_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_standard_args.end, argv[0]);
    return 1;
  }
  
  const char* std_str = clock_standard_args.standard->sval[0];
  tempo_clock_standard_t standard;
  
  if (strcmp(std_str, "24ppqn") == 0 || strcmp(std_str, "din") == 0) {
    standard = CLOCK_STANDARD_24PPQN;
  } else if (strcmp(std_str, "16th") == 0 || strcmp(std_str, "volca") == 0) {
    standard = CLOCK_STANDARD_16TH_NOTE;
  } else if (strcmp(std_str, "beat") == 0 || strcmp(std_str, "modular") == 0) {
    standard = CLOCK_STANDARD_BEAT;
  } else {
    ESP_LOGE(TAG, "Unknown clock standard (use: 24ppqn, 16th, beat)");
    return 1;
  }
  
  tempo_set_clock_standard(standard);
  return 0;
}

// ============================================================================
// LED Commands (merged from led component)
// ============================================================================

// Command: led_on
static int cmd_led_on(int argc, char **argv) {
  ESP_LOGI(TAG, "LED on");
  led_set_on();
  return 0;
}

// Command: led_off
static int cmd_led_off(int argc, char **argv) {
  ESP_LOGI(TAG, "LED off");
  led_set_off();
  return 0;
}

// Command: led_flash
static struct {
  struct arg_int *duration;
  struct arg_end *end;
} led_flash_args;

static int cmd_led_flash(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_flash_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_flash_args.end, argv[0]);
    return 1;
  }
  
  int duration = led_flash_args.duration->ival[0];
  ESP_LOGI(TAG, "Flashing LED for %d ms", duration);
  flash_led((uint32_t)duration);
  
  return 0;
}

// Command: led_enable
static struct {
  struct arg_str *state;
  struct arg_end *end;
} led_enable_args;

static int cmd_led_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_enable_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = led_enable_args.state->sval[0];
  bool enable;
  
  if (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0) {
    enable = true;
  } else if (strcmp(state_str, "off") == 0 || strcmp(state_str, "0") == 0) {
    enable = false;
  } else {
    ESP_LOGE(TAG, "Use: on or off");
    return 1;
  }
  
  led_set_enabled(enable);
  ESP_LOGI(TAG, "LED %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: led_info
static int cmd_led_info(int argc, char **argv) {
  bool enabled = led_get_enabled();
  led_mode_t mode = led_get_mode();
  bool sundial = led_get_sundial_mode();
  bool led_sync = tempo_get_led_sync();
  
  ESP_LOGI(TAG, "====== LED STATUS ======");
  ESP_LOGI(TAG, "Enabled: %s", enabled ? "yes" : "no");
  ESP_LOGI(TAG, "Mode: %s", mode == LED_MODE_DAYLIGHT ? "daylight" : "nighttime");
  ESP_LOGI(TAG, "Sundial mode: %s", sundial ? "yes" : "no");
  ESP_LOGI(TAG, "Tempo sync: %s", led_sync ? "yes" : "no");
  if (led_sync) {
    uint8_t ratio = tempo_get_led_flash_ratio();
    ESP_LOGI(TAG, "  Flash ratio: %d%% of beat", ratio);
  }
  ESP_LOGI(TAG, "========================");
  
  return 0;
}

// Command: led_mode
static struct {
  struct arg_str *mode_name;
  struct arg_end *end;
} led_mode_args;

static int cmd_led_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = led_mode_args.mode_name->sval[0];
  
  if (strcmp(mode_str, "daylight") == 0 || strcmp(mode_str, "day") == 0) {
    led_set_mode(LED_MODE_DAYLIGHT);
    ESP_LOGI(TAG, "LED mode: Daylight (off by default)");
  } else if (strcmp(mode_str, "nighttime") == 0 || strcmp(mode_str, "night") == 0) {
    led_set_mode(LED_MODE_NIGHTTIME);
    ESP_LOGI(TAG, "LED mode: Nighttime (on by default, inverted)");
  } else {
    ESP_LOGE(TAG, "Unknown mode: %s (use 'daylight' or 'nighttime')", mode_str);
    return 1;
  }
  
  return 0;
}

// Command: led_sundial
static struct {
  struct arg_str *state;
  struct arg_end *end;
} led_sundial_args;

static int cmd_led_sundial(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &led_sundial_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, led_sundial_args.end, argv[0]);
    return 1;
  }
  
  const char* state_str = led_sundial_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  led_set_sundial_mode(enable);
  ESP_LOGI(TAG, "Sundial mode: %s (auto day/night based on ambient light)", enable ? "enabled" : "disabled");
  
  return 0;
}

esp_err_t tempo_console_init(void) {
  ESP_LOGI(TAG, "Registering tempo commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show global tempo settings and runtime state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // bpm command
  bpm_args.value = arg_int1(NULL, NULL, "<20-300>", "BPM value");
  bpm_args.end = arg_end(2);
  
  const esp_console_cmd_t bpm_cmd = {
    .command = "bpm",
    .help = "Set BPM (temporary - use 'scene bpm' to persist)",
    .hint = NULL,
    .func = &cmd_bpm,
    .argtable = &bpm_args
  };
  esp_console_cmd_register(&bpm_cmd);
  
  // tap command
  const esp_console_cmd_t tap_cmd = {
    .command = "tap",
    .help = "Register a tap (moving-average BPM, resets after inter-tap timeout)",
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
  
  // clock_standard command
  clock_standard_args.standard = arg_str1(NULL, NULL, "<standard>", "Clock standard");
  clock_standard_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_standard_cmd = {
    .command = "clock_standard",
    .help = "Set clock output standard (24ppqn/16th/beat)",
    .hint = NULL,
    .func = &cmd_clock_standard,
    .argtable = &clock_standard_args
  };
  esp_console_cmd_register(&clock_standard_cmd);
  
  // ========== LED Commands ==========
  
  // led_on command
  const esp_console_cmd_t led_on_cmd = {
    .command = "led_on",
    .help = "Turn LED on",
    .hint = NULL,
    .func = &cmd_led_on,
  };
  esp_console_cmd_register(&led_on_cmd);
  
  // led_off command
  const esp_console_cmd_t led_off_cmd = {
    .command = "led_off",
    .help = "Turn LED off",
    .hint = NULL,
    .func = &cmd_led_off,
  };
  esp_console_cmd_register(&led_off_cmd);
  
  // led_flash command
  led_flash_args.duration = arg_int1(NULL, NULL, "<ms>", "Flash duration in ms");
  led_flash_args.end = arg_end(2);
  
  const esp_console_cmd_t led_flash_cmd = {
    .command = "led_flash",
    .help = "Flash LED",
    .hint = NULL,
    .func = &cmd_led_flash,
    .argtable = &led_flash_args
  };
  esp_console_cmd_register(&led_flash_cmd);
  
  // led_enable command
  led_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  led_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t led_enable_cmd = {
    .command = "led_enable",
    .help = "Enable/disable LED",
    .hint = NULL,
    .func = &cmd_led_enable,
    .argtable = &led_enable_args
  };
  esp_console_cmd_register(&led_enable_cmd);
  
  // led_info command
  const esp_console_cmd_t led_info_cmd = {
    .command = "led_info",
    .help = "Show LED status",
    .hint = NULL,
    .func = &cmd_led_info,
  };
  esp_console_cmd_register(&led_info_cmd);
  
  // led_mode command
  led_mode_args.mode_name = arg_str1(NULL, NULL, "<daylight|nighttime>", "LED mode");
  led_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t led_mode_cmd = {
    .command = "led_mode",
    .help = "Set LED mode (daylight/nighttime)",
    .hint = NULL,
    .func = &cmd_led_mode,
    .argtable = &led_mode_args
  };
  esp_console_cmd_register(&led_mode_cmd);
  
  // led_sundial command
  led_sundial_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  led_sundial_args.end = arg_end(2);
  
  const esp_console_cmd_t led_sundial_cmd = {
    .command = "led_sundial",
    .help = "Auto day/night mode based on ambient light",
    .hint = NULL,
    .func = &cmd_led_sundial,
    .argtable = &led_sundial_args
  };
  esp_console_cmd_register(&led_sundial_cmd);
  
  return ESP_OK;
}

void tempo_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering tempo commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

