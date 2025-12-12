#include "device_config_console.h"
#include "device_config.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char* TAG = "device_config_console";

static const char* registered_commands[] = {
  "info", "trs", "pedal", "company", "program", "pc_mode", "bank_mode", "preset_base", "save"
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

static int cmd_pedal(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pedal_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pedal_args.end, argv[0]);
    return 1;
  }
  
  const char* slug = pedal_args.pedal_slug->sval[0];
  
  // Validate slug exists in database
  if (!slug_exists_in_database(slug)) {
    ESP_LOGE(TAG, "Unknown pedal: %s", slug);
    ESP_LOGI(TAG, "Use 'company' to list available vendors, or 'company <vendor>' to list pedals");
    return 1;
  }
  
  esp_err_t ret = device_config_set_pedal(slug);
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Pedal set to: %s", slug);
  } else {
    ESP_LOGE(TAG, "Failed to set pedal: %s", esp_err_to_name(ret));
  }
  
  return (ret == ESP_OK) ? 0 : 1;
}

// Command: company - list vendors or pedals by vendor
static struct {
  struct arg_str *vendor;
  struct arg_end *end;
} company_args;

static int cmd_company(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &company_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, company_args.end, argv[0]);
    return 1;
  }
  
  uint32_t count = assets_get_device_count();
  
  if (company_args.vendor->count == 0) {
    // No vendor specified - list unique vendors
    ESP_LOGI(TAG, "====== VENDORS (%u devices total) ======", (unsigned)count);
    
    // Collect unique vendors (simple O(n²) approach, fine for small lists)
    const char* seen_vendors[128];
    int seen_count = 0;
    
    for (uint32_t i = 0; i < count && seen_count < 128; i++) {
      const char* vendor = NULL;
      if (assets_get_device_info(i, NULL, NULL, &vendor) == ESP_OK && vendor) {
        // Check if we've seen this vendor
        bool found = false;
        for (int j = 0; j < seen_count; j++) {
          if (strcmp(seen_vendors[j], vendor) == 0) {
            found = true;
            break;
          }
        }
        if (!found) {
          seen_vendors[seen_count++] = vendor;
          // Count pedals for this vendor
          int pedal_count = 0;
          for (uint32_t k = 0; k < count; k++) {
            const char* v = NULL;
            if (assets_get_device_info(k, NULL, NULL, &v) == ESP_OK && v) {
              if (strcmp(v, vendor) == 0) pedal_count++;
            }
          }
          ESP_LOGI(TAG, "  %s (%d)", vendor, pedal_count);
        }
      }
    }
    ESP_LOGI(TAG, "========================================");
  } else {
    // Vendor specified - list pedals by that vendor
    const char* filter_vendor = company_args.vendor->sval[0];
    ESP_LOGI(TAG, "====== PEDALS by %s ======", filter_vendor);
    
    bool found_any = false;
    for (uint32_t i = 0; i < count; i++) {
      const char* slug = NULL;
      const char* name = NULL;
      const char* vendor = NULL;
      if (assets_get_device_info(i, &slug, &name, &vendor) == ESP_OK) {
        if (vendor && strcmp(vendor, filter_vendor) == 0) {
          found_any = true;
          // Extract version from slug (format: vendor.model@version)
          const char* at = strchr(slug, '@');
          const char* version = at ? at + 1 : "0";
          ESP_LOGI(TAG, "  %s [v%s]", slug, version);
        }
      }
    }
    
    if (!found_any) {
      ESP_LOGW(TAG, "No pedals found for vendor: %s", filter_vendor);
      ESP_LOGI(TAG, "Use 'company' with no arguments to list available vendors");
    }
    ESP_LOGI(TAG, "==============================");
  }
  
  return 0;
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

// Command: preset_base
static struct {
  struct arg_int *base;
  struct arg_end *end;
} preset_base_args;

static int cmd_preset_base(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &preset_base_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, preset_base_args.end, argv[0]);
    return 1;
  }
  
  int base = preset_base_args.base->ival[0];
  if (base != 0 && base != 1) {
    ESP_LOGE(TAG, "Invalid base: %d (must be 0 or 1)", base);
    return 1;
  }
  
  esp_err_t ret = device_config_set_preset_base((uint8_t)base);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set preset base: %s", esp_err_to_name(ret));
    return 1;
  }
  
  ESP_LOGI(TAG, "Preset display is now %d-based (PC 0 displays as %d)", base, base);
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
  
  // pedal command
  pedal_args.pedal_slug = arg_str1(NULL, NULL, "<slug>", "e.g. chase_bliss.mood_mkii@0");
  pedal_args.end = arg_end(2);
  
  const esp_console_cmd_t pedal_cmd = {
    .command = "pedal",
    .help = "Set pedal from database (use 'company' to browse)",
    .hint = NULL,
    .func = &cmd_pedal,
    .argtable = &pedal_args
  };
  esp_console_cmd_register(&pedal_cmd);
  
  // company command
  company_args.vendor = arg_str0(NULL, NULL, "[vendor]", "Vendor name to filter by");
  company_args.end = arg_end(2);
  
  const esp_console_cmd_t company_cmd = {
    .command = "company",
    .help = "List vendors, or pedals by vendor",
    .hint = NULL,
    .func = &cmd_company,
    .argtable = &company_args
  };
  esp_console_cmd_register(&company_cmd);
  
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
  
  // preset_base command
  preset_base_args.base = arg_int1(NULL, NULL, "<0|1>", "Display base (0 or 1)");
  preset_base_args.end = arg_end(2);
  
  const esp_console_cmd_t preset_base_cmd = {
    .command = "preset_base",
    .help = "Set preset display base (0=0-based, 1=1-based for Meris etc)",
    .hint = NULL,
    .func = &cmd_preset_base,
    .argtable = &preset_base_args
  };
  esp_console_cmd_register(&preset_base_cmd);
  
  return ESP_OK;
}

void device_config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering device_config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

