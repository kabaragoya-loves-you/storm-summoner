#include "device_config_console.h"
#include "device_config.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "device_config_console";

static const char* registered_commands[] = {
  "info", "trs", "mode", "pedal", "custom", "program", "pc_mode", "bank_mode", "save"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  const device_config_t* cfg = device_config_get();
  
  const char* mode_str = (cfg->mode == DEVICE_MODE_DATABASE) ? "Database" : "Custom";
  const char* trs_str = (cfg->trs_type == MIDI_TRS_TYPE_A) ? "Type A" : "Type B";
  
  const char* pc_mode_str = (cfg->pc_mode == PC_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  
  const char* bank_mode_str = "PC only";
  switch (cfg->bank_select_mode) {
    case BANK_SELECT_CC0: bank_mode_str = "CC0+PC"; break;
    case BANK_SELECT_CC0_CC32: bank_mode_str = "CC0+CC32+PC"; break;
    default: break;
  }
  
  ESP_LOGI(TAG, "====== DEVICE CONFIG ======");
  ESP_LOGI(TAG, "Mode: %s", mode_str);
  ESP_LOGI(TAG, "MIDI Channel: %d", cfg->midi_channel);
  ESP_LOGI(TAG, "TRS Type: %s", trs_str);
  ESP_LOGI(TAG, "Bank Mode: %s", bank_mode_str);
  
  if (cfg->bank_select_mode != BANK_SELECT_NONE) {
    uint16_t preset = (cfg->current_bank * 128) + cfg->current_program;
    ESP_LOGI(TAG, "Current Preset: %u (Bank %d, Program %d)", 
             (unsigned)preset, cfg->current_bank, cfg->current_program);
  } else {
    ESP_LOGI(TAG, "Current Program: %d", cfg->current_program);
  }
  
  ESP_LOGI(TAG, "PC Mode: %s", pc_mode_str);
  
  if (cfg->has_pending_program) {
    if (cfg->bank_select_mode != BANK_SELECT_NONE) {
      uint16_t pending = (cfg->pending_bank * 128) + cfg->pending_program;
      ESP_LOGI(TAG, "PENDING PRESET: %u (Bank %d, Program %d)", 
               (unsigned)pending, cfg->pending_bank, cfg->pending_program);
    } else {
      ESP_LOGI(TAG, "PENDING PROGRAM: %d", cfg->pending_program);
    }
  }
  
  ESP_LOGI(TAG, "");
  if (cfg->mode == DEVICE_MODE_DATABASE) {
    ESP_LOGI(TAG, "Pedal: %s", cfg->pedal_slug);
  } else {
    ESP_LOGI(TAG, "Custom Name: %s", cfg->custom_name);
  }
  ESP_LOGI(TAG, "==========================");
  
  return 0;
}

// Note: channel command moved to midi context

// Command: program
static struct {
  struct arg_int *program_num;
  struct arg_end *end;
} program_args;

// Command: pc_mode
static struct {
  struct arg_str *mode_name;
  struct arg_end *end;
} pc_mode_args;

// Command: bank_mode
static struct {
  struct arg_str *mode_name;
  struct arg_end *end;
} bank_mode_args;

// Note: cmd_channel moved to midi context

// Command: trs
static struct {
  struct arg_str *trs_type;
  struct arg_end *end;
} trs_args;

static int cmd_trs(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &trs_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, trs_args.end, argv[0]);
    return 1;
  }
  
  const char* type_str = trs_args.trs_type->sval[0];
  midi_trs_type_t type;
  
  if (strcmp(type_str, "a") == 0 || strcmp(type_str, "A") == 0) {
    type = MIDI_TRS_TYPE_A;
  } else if (strcmp(type_str, "b") == 0 || strcmp(type_str, "B") == 0) {
    type = MIDI_TRS_TYPE_B;
  } else {
    ESP_LOGE(TAG, "Unknown TRS type. Use: a or b");
    return 1;
  }
  
  esp_err_t ret = device_config_set_trs_type(type);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "TRS type set to %s", (type == MIDI_TRS_TYPE_A) ? "Type A" : "Type B");
  } else {
    ESP_LOGE(TAG, "Failed to set TRS type: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
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
  const device_config_t* cfg = device_config_get();
  
  if (strcmp(mode_str, "database") == 0) {
    ESP_LOGI(TAG, "Mode: Database (current pedal: %s)", cfg->pedal_slug);
  } else if (strcmp(mode_str, "custom") == 0) {
    ESP_LOGI(TAG, "Mode: Custom (current name: %s)", cfg->custom_name);
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: database or custom");
    return 1;
  }
  
  return 0;
}

// Command: pedal
static struct {
  struct arg_str *pedal_slug;
  struct arg_end *end;
} pedal_args;

static int cmd_pedal(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pedal_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pedal_args.end, argv[0]);
    return 1;
  }
  
  const char* slug = pedal_args.pedal_slug->sval[0];
  esp_err_t ret = device_config_set_pedal(slug);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Pedal set to: %s", slug);
  } else {
    ESP_LOGE(TAG, "Failed to set pedal: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: custom
static struct {
  struct arg_str *custom_name;
  struct arg_end *end;
} custom_args;

static int cmd_custom(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &custom_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, custom_args.end, argv[0]);
    return 1;
  }
  
  const char* name = custom_args.custom_name->sval[0];
  esp_err_t ret = device_config_set_custom(name);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Custom device name set to: %s", name);
  } else {
    ESP_LOGE(TAG, "Failed to set custom name: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: save
static int cmd_save(int argc, char **argv) {
  esp_err_t ret = device_config_save();
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Device configuration saved to NVS");
  } else {
    ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

static int cmd_program(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &program_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, program_args.end, argv[0]);
    return 1;
  }
  
  int prog = program_args.program_num->ival[0];
  if (prog < 0 || prog > 127) {
    ESP_LOGE(TAG, "Program must be 0-127");
    return 1;
  }
  
  device_config_set_program(prog);
  ESP_LOGI(TAG, "Program set to %d (PC sent)", prog);
  return 0;
}

static int cmd_pc_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pc_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pc_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode = pc_mode_args.mode_name->sval[0];
  
  if (strcmp(mode, "immediate") == 0) {
    device_config_set_pc_mode(PC_MODE_IMMEDIATE);
    ESP_LOGI(TAG, "PC mode: Immediate");
  } else if (strcmp(mode, "pending") == 0) {
    device_config_set_pc_mode(PC_MODE_PENDING);
    ESP_LOGI(TAG, "PC mode: Pending");
  } else {
    ESP_LOGE(TAG, "Unknown mode: %s (use 'immediate' or 'pending')", mode);
    return 1;
  }
  
  return 0;
}

static int cmd_bank_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &bank_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, bank_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode = bank_mode_args.mode_name->sval[0];
  bank_select_mode_t bank_mode;
  
  if (strcmp(mode, "none") == 0 || strcmp(mode, "pc") == 0) {
    bank_mode = BANK_SELECT_NONE;
  } else if (strcmp(mode, "cc0") == 0) {
    bank_mode = BANK_SELECT_CC0;
  } else if (strcmp(mode, "cc0_cc32") == 0 || strcmp(mode, "cc0+cc32") == 0) {
    bank_mode = BANK_SELECT_CC0_CC32;
  } else {
    ESP_LOGE(TAG, "Unknown mode: %s (use 'none', 'cc0', or 'cc0_cc32')", mode);
    return 1;
  }
  
  esp_err_t ret = device_config_set_bank_mode(bank_mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set bank mode: %s", esp_err_to_name(ret));
    return 1;
  }
  
  return 0;
}

esp_err_t device_config_console_init(void) {
  ESP_LOGI(TAG, "Registering device_config commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show device configuration",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // Note: channel command moved to midi context
  
  // trs command
  trs_args.trs_type = arg_str1(NULL, NULL, "<a|b>", "TRS wiring type");
  trs_args.end = arg_end(2);
  
  const esp_console_cmd_t trs_cmd = {
    .command = "trs",
    .help = "Set TRS wiring type",
    .hint = NULL,
    .func = &cmd_trs,
    .argtable = &trs_args
  };
  esp_console_cmd_register(&trs_cmd);
  
  // mode command
  mode_args.mode_type = arg_str1(NULL, NULL, "<database|custom>", "Device mode");
  mode_args.end = arg_end(2);
  
  const esp_console_cmd_t mode_cmd = {
    .command = "mode",
    .help = "Show device mode",
    .hint = NULL,
    .func = &cmd_mode,
    .argtable = &mode_args
  };
  esp_console_cmd_register(&mode_cmd);
  
  // pedal command
  pedal_args.pedal_slug = arg_str1(NULL, NULL, "<slug>", "Pedal slug from database");
  pedal_args.end = arg_end(2);
  
  const esp_console_cmd_t pedal_cmd = {
    .command = "pedal",
    .help = "Set pedal from database",
    .hint = NULL,
    .func = &cmd_pedal,
    .argtable = &pedal_args
  };
  esp_console_cmd_register(&pedal_cmd);
  
  // custom command
  custom_args.custom_name = arg_str1(NULL, NULL, "<name>", "Custom device name");
  custom_args.end = arg_end(2);
  
  const esp_console_cmd_t custom_cmd = {
    .command = "custom",
    .help = "Set custom device name",
    .hint = NULL,
    .func = &cmd_custom,
    .argtable = &custom_args
  };
  esp_console_cmd_register(&custom_cmd);
  
  // save command
  const esp_console_cmd_t save_cmd = {
    .command = "save",
    .help = "Save configuration to NVS",
    .hint = NULL,
    .func = &cmd_save,
  };
  esp_console_cmd_register(&save_cmd);
  
  // program command
  program_args.program_num = arg_int1(NULL, NULL, "<0-127>", "Program number");
  program_args.end = arg_end(2);
  
  const esp_console_cmd_t program_cmd = {
    .command = "program",
    .help = "Set current program (sends PC)",
    .hint = NULL,
    .func = &cmd_program,
    .argtable = &program_args
  };
  esp_console_cmd_register(&program_cmd);
  
  // pc_mode command
  pc_mode_args.mode_name = arg_str1(NULL, NULL, "<immediate|pending>", "PC mode");
  pc_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t pc_mode_cmd = {
    .command = "pc_mode",
    .help = "Set program change mode",
    .hint = NULL,
    .func = &cmd_pc_mode,
    .argtable = &pc_mode_args
  };
  esp_console_cmd_register(&pc_mode_cmd);
  
  // bank_mode command
  bank_mode_args.mode_name = arg_str1(NULL, NULL, "<none|cc0|cc0_cc32>", "Bank select mode");
  bank_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t bank_mode_cmd = {
    .command = "bank_mode",
    .help = "Set bank select protocol (none=PC only, cc0=CC0+PC, cc0_cc32=CC0+CC32+PC)",
    .hint = NULL,
    .func = &cmd_bank_mode,
    .argtable = &bank_mode_args
  };
  esp_console_cmd_register(&bank_mode_cmd);
  
  return ESP_OK;
}

void device_config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering device_config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

