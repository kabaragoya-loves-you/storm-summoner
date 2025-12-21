#include "device_config_console.h"
#include "device_config.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "device_config_console";

static const char* registered_commands[] = {
  "info", "pedal", "program"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  const device_config_t* cfg = device_config_get();
  
  const char* trs_str = "Unknown";
  switch (cfg->trs_type) {
    case MIDI_TRS_TYPE_A: trs_str = "Type A"; break;
    case MIDI_TRS_TYPE_B: trs_str = "Type B"; break;
    case MIDI_TRS_TYPE_TS: trs_str = "Type TS"; break;
    case MIDI_TRS_TYPE_BOTH: trs_str = "Both (A+B)"; break;
    default: break;
  }
  const char* pc_mode_str = (cfg->pc_mode == PC_MODE_IMMEDIATE) ? "Immediate" : "Pending";
  
  const char* bank_mode_str = "PC only";
  switch (cfg->bank_select_mode) {
    case BANK_SELECT_CC0: bank_mode_str = "CC0+PC"; break;
    case BANK_SELECT_CC0_CC32: bank_mode_str = "CC0+CC32+PC"; break;
    default: break;
  }
  
  ESP_LOGI(TAG, "====== DEVICE CONFIG ======");
  ESP_LOGI(TAG, "Pedal: %s", cfg->pedal_slug[0] ? cfg->pedal_slug : "(none)");
  ESP_LOGI(TAG, "MIDI Channel: %d", cfg->midi_channel);
  ESP_LOGI(TAG, "TRS Type: %s", trs_str);
  ESP_LOGI(TAG, "Bank Mode: %s", bank_mode_str);
  ESP_LOGI(TAG, "Preset Count: %u (range lock: %s)", 
           (unsigned)cfg->preset_count, cfg->lock_preset_range ? "ON" : "OFF");
  ESP_LOGI(TAG, "Preset Base: %d (%s)", cfg->preset_base, 
           cfg->preset_base == 0 ? "0-based" : "1-based");
  
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
  ESP_LOGI(TAG, "==========================");
  
  return 0;
}

// Command: pedal
static struct {
  struct arg_str *pedal_slug;
  struct arg_end *end;
} pedal_args;

// Helper to check if a slug exists in the device database
static bool slug_exists_in_database(const char* slug) {
  uint32_t count = assets_get_device_count();
  for (uint32_t i = 0; i < count; i++) {
    const char* db_slug = NULL;
    if (assets_get_device_info(i, &db_slug, NULL, NULL) == ESP_OK) {
      if (db_slug && strcmp(db_slug, slug) == 0) return true;
    }
  }
  return false;
}

// Helper to list available vendors and pedals
static void list_available_pedals(void) {
  uint32_t vendor_count = assets_get_vendor_count();
  ESP_LOGI(TAG, "Available vendors (%u):", (unsigned)vendor_count);
  
  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* vendor = assets_get_vendor_by_index(i);
    if (vendor) {
      uint32_t pedal_count = assets_get_device_count_for_vendor(vendor);
      ESP_LOGI(TAG, "  %s (%u pedals)", vendor, (unsigned)pedal_count);
    }
  }
}

static int cmd_pedal(int argc, char **argv) {
  // If no arguments, list available pedals
  if (argc == 1) {
    list_available_pedals();
    ESP_LOGI(TAG, "Usage: pedal <slug> (e.g., chase_bliss.mood_mkii@0)");
    return 0;
  }
  
  int nerrors = arg_parse(argc, argv, (void **) &pedal_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pedal_args.end, argv[0]);
    return 1;
  }
  
  const char* slug = pedal_args.pedal_slug->sval[0];
  
  // Validate slug exists in database
  if (!slug_exists_in_database(slug)) {
    ESP_LOGE(TAG, "Unknown pedal: %s", slug);
    list_available_pedals();
    return 1;
  }
  
  // device_config_set_pedal automatically saves to NVS
  esp_err_t ret = device_config_set_pedal(slug);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Pedal set to: %s (saved)", slug);
  } else {
    ESP_LOGE(TAG, "Failed to set pedal: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: preset_lock
static struct {
  struct arg_str *on_off;
  struct arg_end *end;
} preset_lock_args;

static int cmd_preset_lock(int argc, char **argv) {
  // If no arguments, show current status
  if (argc == 1) {
    bool locked = device_config_get_lock_preset_range();
    uint16_t count = device_config_get_preset_count();
    uint16_t max = device_config_get_max_preset();
    ESP_LOGI(TAG, "Preset range lock: %s", locked ? "ON" : "OFF");
    ESP_LOGI(TAG, "Preset count: %u, max preset: %u", (unsigned)count, (unsigned)max);
    return 0;
  }
  
  int nerrors = arg_parse(argc, argv, (void **) &preset_lock_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, preset_lock_args.end, argv[0]);
    return 1;
  }
  
  const char* val = preset_lock_args.on_off->sval[0];
  bool lock;
  
  if (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0) {
    lock = true;
  } else if (strcmp(val, "off") == 0 || strcmp(val, "0") == 0 || strcmp(val, "false") == 0) {
    lock = false;
  } else {
    ESP_LOGE(TAG, "Invalid value: %s (use on/off)", val);
    return 1;
  }
  
  device_config_set_lock_preset_range(lock);
  return 0;
}

// Command: program
static struct {
  struct arg_int *program_num;
  struct arg_end *end;
} program_args;

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
  
  // pedal command
  pedal_args.pedal_slug = arg_str0(NULL, NULL, "<slug>", "e.g. chase_bliss.mood_mkii@0");
  pedal_args.end = arg_end(2);
  
  const esp_console_cmd_t pedal_cmd = {
    .command = "pedal",
    .help = "Set pedal from database, or list available vendors",
    .hint = NULL,
    .func = &cmd_pedal,
    .argtable = &pedal_args
  };
  esp_console_cmd_register(&pedal_cmd);
  
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
  
  // preset_lock command
  preset_lock_args.on_off = arg_str0(NULL, NULL, "<on|off>", "Enable/disable preset range lock");
  preset_lock_args.end = arg_end(2);
  
  const esp_console_cmd_t preset_lock_cmd = {
    .command = "preset_lock",
    .help = "Lock presets to device's defined range (shows status if no arg)",
    .hint = NULL,
    .func = &cmd_preset_lock,
    .argtable = &preset_lock_args
  };
  esp_console_cmd_register(&preset_lock_cmd);
  
  return ESP_OK;
}

void device_config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering device_config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
