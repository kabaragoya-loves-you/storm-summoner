#include "device_config_console.h"
#include "device_config.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char* TAG = "device_config_console";

static const char* registered_commands[] = {
  "info", "pedal", "vendor", "program"
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
  ESP_LOGI(TAG, "Preset Count: %u", (unsigned)cfg->preset_count);
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

// Helper to list available vendors
static void list_available_vendors(void) {
  uint32_t vendor_count = assets_get_vendor_count();
  ESP_LOGI(TAG, "Available vendors (%u):", (unsigned)vendor_count);

  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* vendor = assets_get_vendor_by_index(i);
    if (vendor) {
      uint32_t pedal_count = assets_get_device_count_for_vendor(vendor);
      ESP_LOGI(TAG, "  %s (%u pedals)", vendor, (unsigned)pedal_count);
    }
  }
  ESP_LOGI(TAG, "Usage: vendor <name>  (e.g., vendor Chase Bliss)");
}

// Join argv[1..] into a single vendor name (supports multi-word names)
static bool join_vendor_name(int argc, char **argv, char *out, size_t out_len) {
  if (argc < 2 || !out || out_len == 0) return false;

  size_t pos = 0;
  out[0] = '\0';
  for (int i = 1; i < argc; i++) {
    int written = snprintf(out + pos, out_len - pos, "%s%s",
      (pos > 0) ? " " : "", argv[i]);
    if (written < 0 || (size_t)written >= out_len - pos) {
      out[out_len - 1] = '\0';
      return false;
    }
    pos += (size_t)written;
  }
  return pos > 0;
}

// Resolve a typed vendor name to the canonical manifest name (case-insensitive)
static const char* resolve_vendor_name(const char* typed) {
  if (!typed || typed[0] == '\0') return NULL;

  uint32_t vendor_count = assets_get_vendor_count();
  for (uint32_t i = 0; i < vendor_count; i++) {
    const char* vendor = assets_get_vendor_by_index(i);
    if (vendor && strcasecmp(vendor, typed) == 0) return vendor;
  }
  return NULL;
}

static void list_pedals_for_vendor(const char* vendor) {
  uint32_t pedal_count = assets_get_device_count_for_vendor(vendor);
  ESP_LOGI(TAG, "%s (%u pedals):", vendor, (unsigned)pedal_count);

  for (uint32_t i = 0; i < pedal_count; i++) {
    const char* slug = NULL;
    const char* name = NULL;
    if (assets_get_device_for_vendor(vendor, i, &slug, &name) == ESP_OK) {
      ESP_LOGI(TAG, "  %s  (%s)", slug ? slug : "?", name ? name : "?");
    }
  }
  ESP_LOGI(TAG, "Usage: pedal <slug>  (e.g., pedal chase_bliss.mood_mkii@0)");
}

static int cmd_vendor(int argc, char **argv) {
  if (argc == 1) {
    list_available_vendors();
    return 0;
  }

  char typed[128];
  if (!join_vendor_name(argc, argv, typed, sizeof(typed))) {
    ESP_LOGE(TAG, "Vendor name too long");
    return 1;
  }

  const char* vendor = resolve_vendor_name(typed);
  if (!vendor) {
    ESP_LOGE(TAG, "Unknown vendor: %s", typed);
    list_available_vendors();
    return 1;
  }

  list_pedals_for_vendor(vendor);
  return 0;
}

static int cmd_pedal(int argc, char **argv) {
  if (argc == 1) {
    ESP_LOGI(TAG, "Usage: pedal <slug>  (e.g., pedal chase_bliss.mood_mkii@0)");
    ESP_LOGI(TAG, "To browse pedals, use: vendor  or  vendor <name>");
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
    ESP_LOGI(TAG, "To browse pedals, use: vendor  or  vendor <name>");
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
    .help = "Set pedal by slug (use 'vendor' to browse)",
    .hint = NULL,
    .func = &cmd_pedal,
    .argtable = &pedal_args
  };
  esp_console_cmd_register(&pedal_cmd);

  // vendor command
  const esp_console_cmd_t vendor_cmd = {
    .command = "vendor",
    .help = "List vendors, or list pedals for a vendor (multi-word names OK)",
    .hint = NULL,
    .func = &cmd_vendor,
  };
  esp_console_cmd_register(&vendor_cmd);

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
  
  return ESP_OK;
}

void device_config_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering device_config commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}
