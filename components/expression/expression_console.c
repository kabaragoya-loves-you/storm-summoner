#include "expression_console.h"
#include "expression.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static const char* TAG = "expression_console";

static const char* registered_commands[] = {
  "info", "mode", "calibrate", "polarity", "switch_type", "gate_log"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  bool connected = expression_is_connected();
  expression_mode_t mode = expression_get_mode();
  expression_polarity_t polarity = expression_get_polarity();
  pedal_switch_type_t switch_type = expression_get_pedal_switch_type();
  int16_t min, max;
  expression_get_range(&min, &max);
  uint8_t deadzone = expression_get_deadzone();
  
  const char* mode_str = (mode == EXPRESSION_MODE_NONE) ? "Disabled" :
                         (mode == EXPRESSION_MODE_PEDAL) ? "Expression Pedal" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "Sustain Pedal" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "Sostenuto Pedal" :
                         (mode == EXPRESSION_MODE_SWITCH) ? "Switch" : "Gate";
  const char* polarity_str = (polarity == EXPRESSION_POLARITY_TIP_ADC) ? "Tip->ADC, Ring->VCC" : "Ring->ADC, Tip->VCC";
  const char* switch_str = (switch_type == PEDAL_SWITCH_NO) ? "NO (Normally Open)" : "NC (Normally Closed)";
  
  ESP_LOGI(TAG, "====== EXPRESSION JACK ======");
  ESP_LOGI(TAG, "Hardware mode: %s", mode_str);
  ESP_LOGI(TAG, "Cable: %s", connected ? "connected" : "disconnected");
  ESP_LOGI(TAG, "");
  
  if (mode == EXPRESSION_MODE_NONE) {
    ESP_LOGI(TAG, "Expression jack is disabled for this scene");
  } else if (mode == EXPRESSION_MODE_PEDAL) {
    ESP_LOGI(TAG, "TRS polarity: %s", polarity_str);
    ESP_LOGI(TAG, "Calibration: %d to %d", min, max);
    ESP_LOGI(TAG, "Deadzone: %d", deadzone);
    uint8_t midi_val = expression_get_midi_value();
    ESP_LOGI(TAG, "Current value: %d (0-127)", midi_val);
  } else if (mode == EXPRESSION_MODE_SUSTAIN || mode == EXPRESSION_MODE_SOSTENUTO) {
    ESP_LOGI(TAG, "Switch type: %s", switch_str);
  } else if (mode == EXPRESSION_MODE_GATE) {
    bool gate_state = expression_get_gate_state();
    bool gate_logging = expression_get_gate_logging();
    ESP_LOGI(TAG, "Gate state: %s", gate_state ? "HIGH" : "LOW");
    ESP_LOGI(TAG, "Gate logging: %s", gate_logging ? "enabled" : "disabled");
  } else if (mode == EXPRESSION_MODE_SWITCH) {
    ESP_LOGI(TAG, "Switch type: %s", switch_str);
    ESP_LOGI(TAG, "Action chain: (configured in scene context via expr_switch command)");
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Note: Mode and MIDI routing set in scene context");
  ESP_LOGI(TAG, "  cd scene -> expr_mode <none|expression|sustain|sostenuto|gate|switch>");
  ESP_LOGI(TAG, "=============================");
  
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
  
  if (strcmp(mode_str, "none") == 0 || strcmp(mode_str, "off") == 0) {
    mode = EXPRESSION_MODE_NONE;
  } else if (strcmp(mode_str, "pedal") == 0) {
    mode = EXPRESSION_MODE_PEDAL;
  } else if (strcmp(mode_str, "sustain") == 0) {
    mode = EXPRESSION_MODE_SUSTAIN;
  } else if (strcmp(mode_str, "sostenuto") == 0) {
    mode = EXPRESSION_MODE_SOSTENUTO;
  } else if (strcmp(mode_str, "gate") == 0) {
    mode = EXPRESSION_MODE_GATE;
  } else if (strcmp(mode_str, "switch") == 0) {
    mode = EXPRESSION_MODE_SWITCH;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: none, pedal, sustain, sostenuto, gate, or switch");
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

// Command: polarity
static struct {
  struct arg_str *polarity_type;
  struct arg_end *end;
} polarity_args;

static int cmd_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, polarity_args.end, argv[0]);
    return 1;
  }
  
  const char* pol_str = polarity_args.polarity_type->sval[0];
  expression_polarity_t polarity;
  
  if (strcmp(pol_str, "tip_adc") == 0 || strcmp(pol_str, "tip") == 0) {
    polarity = EXPRESSION_POLARITY_TIP_ADC;
  } else if (strcmp(pol_str, "ring_adc") == 0 || strcmp(pol_str, "ring") == 0) {
    polarity = EXPRESSION_POLARITY_RING_ADC;
  } else {
    ESP_LOGE(TAG, "Unknown polarity. Use: tip_adc or ring_adc");
    return 1;
  }
  
  expression_set_polarity(polarity);
  ESP_LOGI(TAG, "Expression polarity set to: %s", pol_str);
  
  return 0;
}

// Command: switch_type
static struct {
  struct arg_str *type;
  struct arg_end *end;
} switch_type_args;

static int cmd_switch_type(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &switch_type_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, switch_type_args.end, argv[0]);
    return 1;
  }
  
  const char* type_str = switch_type_args.type->sval[0];
  pedal_switch_type_t type;
  
  if (strcmp(type_str, "no") == 0) {
    type = PEDAL_SWITCH_NO;
  } else if (strcmp(type_str, "nc") == 0) {
    type = PEDAL_SWITCH_NC;
  } else {
    ESP_LOGE(TAG, "Unknown switch type. Use: no or nc");
    return 1;
  }
  
  expression_set_pedal_switch_type(type);
  ESP_LOGI(TAG, "Pedal switch type set to: %s", type_str);
  
  return 0;
}

// Command: gate_log
static struct {
  struct arg_str *enabled;
  struct arg_end *end;
} gate_log_args;

static int cmd_gate_log(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &gate_log_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, gate_log_args.end, argv[0]);
    return 1;
  }
  
  const char* enabled_str = gate_log_args.enabled->sval[0];
  bool enabled;
  
  if (strcmp(enabled_str, "on") == 0 || strcmp(enabled_str, "true") == 0 || strcmp(enabled_str, "1") == 0) {
    enabled = true;
  } else if (strcmp(enabled_str, "off") == 0 || strcmp(enabled_str, "false") == 0 || strcmp(enabled_str, "0") == 0) {
    enabled = false;
  } else {
    ESP_LOGE(TAG, "Invalid value. Use: on/off, true/false, or 1/0");
    return 1;
  }
  
  expression_set_gate_logging(enabled);
  ESP_LOGI(TAG, "Gate logging %s", enabled ? "enabled" : "disabled");
  
  return 0;
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
  mode_args.mode_type = arg_str1(NULL, NULL, "<none|pedal|sustain|sostenuto|gate|switch>", "Expression mode");
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
  
  // polarity command
  polarity_args.polarity_type = arg_str1(NULL, NULL, "<tip_adc|ring_adc>", "TRS polarity");
  polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t polarity_cmd = {
    .command = "polarity",
    .help = "Set TRS polarity (pedal mode only)",
    .hint = NULL,
    .func = &cmd_polarity,
    .argtable = &polarity_args
  };
  esp_console_cmd_register(&polarity_cmd);
  
  // switch_type command
  switch_type_args.type = arg_str1(NULL, NULL, "<no|nc>", "Switch type");
  switch_type_args.end = arg_end(2);
  
  const esp_console_cmd_t switch_type_cmd = {
    .command = "switch_type",
    .help = "Set pedal switch type (sustain/sostenuto mode)",
    .hint = NULL,
    .func = &cmd_switch_type,
    .argtable = &switch_type_args
  };
  esp_console_cmd_register(&switch_type_cmd);
  
  // gate_log command
  gate_log_args.enabled = arg_str1(NULL, NULL, "<on|off>", "Enable/disable gate logging");
  gate_log_args.end = arg_end(2);
  
  const esp_console_cmd_t gate_log_cmd = {
    .command = "gate_log",
    .help = "Enable/disable gate change message logging",
    .hint = NULL,
    .func = &cmd_gate_log,
    .argtable = &gate_log_args
  };
  esp_console_cmd_register(&gate_log_cmd);
  
  return ESP_OK;
}

void expression_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering expression commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

