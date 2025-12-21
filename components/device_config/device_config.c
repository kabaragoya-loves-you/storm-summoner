#include "device_config.h"
#include "assets_manager.h"
#include "app_settings.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "event_bus.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>
#include <stdio.h>

// Default pedal slug when none is configured
#define DEFAULT_PEDAL_SLUG "user.default@0"
#define DEFAULT_DEVICE_DIR "/assets/devices/user"
#define DEFAULT_DEVICE_PATH "/assets/devices/user/default.json"

static const char* TAG = "device_config";

// MIDI event type constants (from midi_in_parser.c)
#define MIDI_EVENT_CONTROL_CHANGE 3
#define MIDI_EVENT_PROGRAM_CHANGE 4

// Bank Select CC numbers
#define CC_BANK_SELECT_MSB 0
#define CC_BANK_SELECT_LSB 32

// Temporary storage for incoming bank select values
// These are applied when a Program Change is received
static uint8_t s_incoming_bank_msb = 0;
static uint8_t s_incoming_bank_lsb = 0;
static bool s_bank_msb_received = false;
static bool s_bank_lsb_received = false;

// NVS keys
#define NVS_KEY_MIDI_CHANNEL    "dev_channel"
#define NVS_KEY_TRS_TYPE        "dev_trs"
#define NVS_KEY_PEDAL_SLUG      "dev_slug"
#define NVS_KEY_CURRENT_PROGRAM "dev_program"
#define NVS_KEY_PC_MODE         "dev_pc_mode"
#define NVS_KEY_BANK_MODE       "dev_bank_mode"
#define NVS_KEY_PRESET_LOCK     "dev_plock"

// Global device configuration
static device_config_t g_device_config = {
  .midi_channel = 1,
  .trs_type = MIDI_TRS_TYPE_BOTH,
  .pedal_slug = "",
  .current_program = 0,
  .pending_program = 0,
  .has_pending_program = false,
  .pc_mode = PC_MODE_IMMEDIATE,
  .bank_select_mode = BANK_SELECT_NONE,
  .current_bank = 0,
  .pending_bank = 0,
  .preset_base = 0,
  .preset_count = 128,
  .lock_preset_range = true,  // Default: respect device's preset count
  .initialized = false
};

// Handle incoming MIDI events to track program changes and bank select
static void midi_in_event_handler(const event_t* event, void* context) {
  if (event->type != EVENT_MIDI_IN) return;
  
  uint8_t midi_channel = event->data.midi_in.channel;  // 0-based
  uint8_t our_channel = g_device_config.midi_channel - 1;
  
  // Only process messages on our configured channel
  if (midi_channel != our_channel) return;
  
  // Handle Control Change messages for bank select (only in bank modes)
  if (event->data.midi_in.type == MIDI_EVENT_CONTROL_CHANGE) {
    uint8_t cc_number = event->data.midi_in.data1;
    uint8_t cc_value = event->data.midi_in.data2;
    
    if (g_device_config.bank_select_mode != BANK_SELECT_NONE) {
      if (cc_number == CC_BANK_SELECT_MSB) {
        s_incoming_bank_msb = cc_value;
        s_bank_msb_received = true;
        ESP_LOGD(TAG, "Bank MSB received: %d", cc_value);
      } else if (cc_number == CC_BANK_SELECT_LSB && 
                 g_device_config.bank_select_mode == BANK_SELECT_CC0_CC32) {
        // Only track LSB if we're in CC0+CC32 mode
        s_incoming_bank_lsb = cc_value;
        s_bank_lsb_received = true;
        ESP_LOGD(TAG, "Bank LSB received: %d", cc_value);
      }
    }
    return;
  }
  
  // Handle Program Change
  if (event->data.midi_in.type == MIDI_EVENT_PROGRAM_CHANGE) {
    uint8_t program = event->data.midi_in.data1;
    
    // Update current program
    g_device_config.current_program = program;
    
    // Apply any pending bank select values (only in bank modes)
    if (g_device_config.bank_select_mode != BANK_SELECT_NONE && s_bank_msb_received) {
      g_device_config.current_bank = s_incoming_bank_msb;
      uint16_t preset = (g_device_config.current_bank * 128) + program;
      ESP_LOGI(TAG, "Preset updated from MIDI IN: %u (bank %d, program %d)", 
               (unsigned)preset, g_device_config.current_bank, program);
    } else {
      ESP_LOGI(TAG, "Program updated from MIDI IN: %d", program);
    }
    
    // Clear pending bank values after PC
    s_incoming_bank_msb = 0;
    s_incoming_bank_lsb = 0;
    s_bank_msb_received = false;
    s_bank_lsb_received = false;
    
    // Clear any pending program state since external change overrides it
    g_device_config.has_pending_program = false;
  }
}

// Default device JSON content (matches midi-devices/devices/user/default.json)
static const char* DEFAULT_DEVICE_JSON = 
  "{\n"
  "  \"schemaVersion\": \"0.1.1\",\n"
  "  \"title\": \"Custom Device\",\n"
  "  \"displayName\": \"Custom Device\",\n"
  "  \"implementationVersion\": \"0\",\n"
  "  \"device\": {\n"
  "    \"displayName\": \"Custom Device\",\n"
  "    \"manufacturer\": \"User\",\n"
  "    \"model\": \"Custom\",\n"
  "    \"version\": \"1.0\"\n"
  "  },\n"
  "  \"receives\": [\"PROGRAM_CHANGE\"],\n"
  "  \"transmits\": [],\n"
  "  \"controlChangeCommands\": [],\n"
  "  \"x_pc\": {\n"
  "    \"indexBase\": 0,\n"
  "    \"count\": 128,\n"
  "    \"bankSelectMode\": \"none\"\n"
  "  },\n"
  "  \"x_midiTrs\": \"BOTH\",\n"
  "  \"x_midiChannel\": 1\n"
  "}\n";

// Ensure the default device JSON exists in LittleFS
static esp_err_t ensure_default_device_exists(void) {
  struct stat st;
  
  // Check if default device already exists
  if (stat(DEFAULT_DEVICE_PATH, &st) == 0) {
    ESP_LOGD(TAG, "Default device already exists");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Creating default device: %s", DEFAULT_DEVICE_PATH);
  
  // Create user directory if it doesn't exist
  if (stat(DEFAULT_DEVICE_DIR, &st) != 0) {
    if (mkdir(DEFAULT_DEVICE_DIR, 0755) != 0) {
      ESP_LOGE(TAG, "Failed to create directory: %s", DEFAULT_DEVICE_DIR);
      return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Created directory: %s", DEFAULT_DEVICE_DIR);
  }
  
  // Write the default device JSON
  FILE* f = fopen(DEFAULT_DEVICE_PATH, "w");
  if (!f) {
    ESP_LOGE(TAG, "Failed to create default device file");
    return ESP_FAIL;
  }
  
  fputs(DEFAULT_DEVICE_JSON, f);
  fclose(f);
  
  ESP_LOGI(TAG, "Default device created successfully");
  
  // Rebuild manifest to include the new device
  esp_err_t ret = assets_rebuild_manifest();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to rebuild manifest after creating default device");
  }
  
  return ESP_OK;
}

esp_err_t device_config_init(void) {
  if (g_device_config.initialized) {
    ESP_LOGW(TAG, "Device config already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing device configuration");
  
  // Ensure default device exists before loading config
  ensure_default_device_exists();
  
  // Try to load from NVS
  uint8_t channel;
  if (app_settings_load_u8(NVS_KEY_MIDI_CHANNEL, &channel) == ESP_OK && channel >= 1 && channel <= 16) {
    g_device_config.midi_channel = channel;
  }
  
  uint8_t trs_val;
  if (app_settings_load_u8(NVS_KEY_TRS_TYPE, &trs_val) == ESP_OK) {
    g_device_config.trs_type = (midi_trs_type_t)trs_val;
  }
  
  char slug[64];
  if (app_settings_load_str(NVS_KEY_PEDAL_SLUG, slug, sizeof(slug)) == ESP_OK && slug[0] != '\0') {
    strncpy(g_device_config.pedal_slug, slug, sizeof(g_device_config.pedal_slug) - 1);
    g_device_config.pedal_slug[sizeof(g_device_config.pedal_slug) - 1] = '\0';
  } else {
    // Default to user.default@0 if no pedal configured
    strncpy(g_device_config.pedal_slug, DEFAULT_PEDAL_SLUG, sizeof(g_device_config.pedal_slug) - 1);
    g_device_config.pedal_slug[sizeof(g_device_config.pedal_slug) - 1] = '\0';
    ESP_LOGI(TAG, "No pedal configured, defaulting to %s", DEFAULT_PEDAL_SLUG);
  }
  
  // Load program tracking
  uint8_t program;
  if (app_settings_load_u8(NVS_KEY_CURRENT_PROGRAM, &program) == ESP_OK && program <= 127) {
    g_device_config.current_program = program;
  }
  
  uint8_t pc_mode_val;
  if (app_settings_load_u8(NVS_KEY_PC_MODE, &pc_mode_val) == ESP_OK) {
    g_device_config.pc_mode = (pc_change_mode_t)pc_mode_val;
  }
  
  // Load bank select mode
  uint8_t bank_mode_val;
  if (app_settings_load_u8(NVS_KEY_BANK_MODE, &bank_mode_val) == ESP_OK) {
    g_device_config.bank_select_mode = (bank_select_mode_t)bank_mode_val;
  }
  
  // Load preset display base (0 or 1)
  uint8_t preset_base_val;
  if (app_settings_load_u8("preset_base", &preset_base_val) == ESP_OK && preset_base_val <= 1) {
    g_device_config.preset_base = preset_base_val;
  } else {
    g_device_config.preset_base = 0;  // Default: 0-based
  }
  
  // Load preset range lock setting
  uint8_t preset_lock_val;
  if (app_settings_load_u8(NVS_KEY_PRESET_LOCK, &preset_lock_val) == ESP_OK) {
    g_device_config.lock_preset_range = (preset_lock_val != 0);
  }
  
  // Load device definition to get preset count, bank mode, etc.
  if (g_device_config.pedal_slug[0] != '\0') {
    device_def_t* device = assets_load_device(g_device_config.pedal_slug);
    if (device) {
      // Get preset info from device
      if (device->pc_info) {
        g_device_config.preset_base = (device->pc_info->index_base > 0)
                                       ? device->pc_info->index_base : 0;
        g_device_config.preset_count = (device->pc_info->count > 0)
                                        ? device->pc_info->count : 128;
        // Bank select mode
        switch (device->pc_info->bank_mode) {
          case PC_BANK_SELECT_CC0:
            g_device_config.bank_select_mode = BANK_SELECT_CC0;
            break;
          case PC_BANK_SELECT_CC0_CC32:
            g_device_config.bank_select_mode = BANK_SELECT_CC0_CC32;
            break;
          default:
            g_device_config.bank_select_mode = BANK_SELECT_NONE;
            break;
        }
        ESP_LOGI(TAG, "Loaded device info: preset_count=%u, bank_mode=%d",
                 (unsigned)g_device_config.preset_count, g_device_config.bank_select_mode);
      }
      assets_free_device(device);
    }
  }
  
  g_device_config.initialized = true;
  
  // Note: TRS type will be applied to MIDI layer when midi_out_init() is called
  // (it queries device_config_get_trs_type())
  
  // Subscribe to MIDI IN events to track program changes
  event_bus_subscribe(EVENT_MIDI_IN, midi_in_event_handler, NULL);
  
  ESP_LOGI(TAG, "Device config initialized: channel=%d, trs=%d, program=%d, pedal=%s",
           g_device_config.midi_channel,
           g_device_config.trs_type,
           g_device_config.current_program,
           g_device_config.pedal_slug[0] ? g_device_config.pedal_slug : "(none)");
  
  return ESP_OK;
}

uint8_t device_config_get_channel(void) {
  return g_device_config.midi_channel;
}

esp_err_t device_config_set_channel(uint8_t channel) {
  if (channel < 1 || channel > 16) {
    ESP_LOGE(TAG, "Invalid MIDI channel %d (must be 1-16)", channel);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.midi_channel = channel;
  ESP_LOGD(TAG, "MIDI channel set to %d", channel);
  
  return app_settings_save_u8(NVS_KEY_MIDI_CHANNEL, channel);
}

midi_trs_type_t device_config_get_trs_type(void) {
  return g_device_config.trs_type;
}

esp_err_t device_config_set_trs_type(midi_trs_type_t type) {
  g_device_config.trs_type = type;
  
  const char* type_str = "Unknown";
  switch (type) {
    case MIDI_TRS_TYPE_A: type_str = "Type A"; break;
    case MIDI_TRS_TYPE_B: type_str = "Type B"; break;
    case MIDI_TRS_TYPE_TS: type_str = "Type TS"; break;
    case MIDI_TRS_TYPE_BOTH: type_str = "Type A+B"; break;
    default: break;
  }
  
  ESP_LOGI(TAG, "TRS wiring type set to %s", type_str);
  
  // Apply to MIDI output layer
  midi_transmit_mode_t mode = (midi_transmit_mode_t)assets_trs_type_to_transmit_mode(type);
  midi_set_uart_transmit_mode(mode);
  
  uint8_t type_val = (uint8_t)type;
  return app_settings_save_u8(NVS_KEY_TRS_TYPE, type_val);
}

esp_err_t device_config_set_pedal(const char* slug) {
  if (!slug) {
    return ESP_ERR_INVALID_ARG;
  }
  
  strncpy(g_device_config.pedal_slug, slug, sizeof(g_device_config.pedal_slug) - 1);
  g_device_config.pedal_slug[sizeof(g_device_config.pedal_slug) - 1] = '\0';
  
  ESP_LOGI(TAG, "Device set to: %s", slug);
  
  // Load device profile and apply settings
  device_def_t *device = assets_load_device(slug);
  if (device) {
    // Apply TRS type via setter (also updates MIDI layer)
    // UNKNOWN means use BOTH as default
    midi_trs_type_t trs = (device->trs_type != MIDI_TRS_UNKNOWN) 
                           ? device->trs_type : MIDI_TRS_TYPE_BOTH;
    device_config_set_trs_type(trs);
    
    // Apply MIDI channel if specified (1-16, 0 means not specified)
    if (device->midi_channel >= 1 && device->midi_channel <= 16) {
      device_config_set_channel(device->midi_channel);
    }
    
    // Apply program change settings from x_pc
    if (device->pc_info) {
      // Preset base (0 or 1)
      g_device_config.preset_base = (device->pc_info->index_base <= 1) 
                                     ? device->pc_info->index_base : 0;
      ESP_LOGI(TAG, "  Preset base: %d", g_device_config.preset_base);
      
      // Preset count (number of presets)
      g_device_config.preset_count = (device->pc_info->count > 0)
                                      ? device->pc_info->count : 128;
      ESP_LOGI(TAG, "  Preset count: %u", (unsigned)g_device_config.preset_count);
      
      // Bank select mode (map from pc_bank_select_mode_t to bank_select_mode_t)
      switch (device->pc_info->bank_mode) {
        case PC_BANK_SELECT_CC0:
          g_device_config.bank_select_mode = BANK_SELECT_CC0;
          break;
        case PC_BANK_SELECT_CC0_CC32:
          g_device_config.bank_select_mode = BANK_SELECT_CC0_CC32;
          break;
        default:
          g_device_config.bank_select_mode = BANK_SELECT_NONE;
          break;
      }
      ESP_LOGI(TAG, "  Bank mode: %d", g_device_config.bank_select_mode);
    } else {
      // No x_pc - set defaults
      g_device_config.preset_base = 0;
      g_device_config.preset_count = 128;
      g_device_config.bank_select_mode = BANK_SELECT_NONE;
      ESP_LOGI(TAG, "  Using defaults: preset_base=0, preset_count=128, bank_mode=none");
    }
    
    assets_free_device(device);
  } else {
    ESP_LOGW(TAG, "Could not load device profile, using current settings");
  }
  
  return app_settings_save_str(NVS_KEY_PEDAL_SLUG, g_device_config.pedal_slug);
}

const char* device_config_get_pedal_slug(void) {
  return g_device_config.pedal_slug;
}

const device_config_t* device_config_get(void) {
  return &g_device_config;
}

esp_err_t device_config_save(void) {
  ESP_LOGI(TAG, "Saving device configuration to NVS");
  
  esp_err_t ret = app_settings_save_u8(NVS_KEY_MIDI_CHANNEL, g_device_config.midi_channel);
  if (ret != ESP_OK) return ret;
  
  uint8_t trs_val = (uint8_t)g_device_config.trs_type;
  ret = app_settings_save_u8(NVS_KEY_TRS_TYPE, trs_val);
  if (ret != ESP_OK) return ret;
  
  ret = app_settings_save_str(NVS_KEY_PEDAL_SLUG, g_device_config.pedal_slug);
  
  return ret;
}

uint8_t device_config_get_program(void) {
  return g_device_config.current_program;
}

// Internal helper to send program change with appropriate bank select messages
static void send_program_with_bank(uint8_t channel, uint8_t bank, uint8_t program) {
  switch (g_device_config.bank_select_mode) {
    case BANK_SELECT_CC0:
      // Send CC0 (Bank Select MSB) then PC
      send_control_change(channel, 0, bank);
      send_program_change(channel, program);
      break;
    case BANK_SELECT_CC0_CC32:
      // Send CC0 (MSB) + CC32 (LSB=0) then PC
      send_bank_select(channel, bank, 0);
      send_program_change(channel, program);
      break;
    case BANK_SELECT_NONE:
    default:
      // Just send PC
      send_program_change(channel, program);
      break;
  }
}

esp_err_t device_config_set_program(uint8_t program) {
  if (program > 127) {
    ESP_LOGE(TAG, "Invalid program number %d (must be 0-127)", program);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.current_program = program;
  
  // Send MIDI program change (with bank if enabled)
  uint8_t channel = g_device_config.midi_channel - 1;
  send_program_with_bank(channel, g_device_config.current_bank, program);
  
  if (g_device_config.bank_select_mode != BANK_SELECT_NONE) {
    ESP_LOGI(TAG, "Program changed to bank %d, program %d (channel %d)", 
             g_device_config.current_bank, program, g_device_config.midi_channel);
  } else {
    ESP_LOGI(TAG, "Program changed to %d (channel %d)", program, g_device_config.midi_channel);
  }
  
  // Do NOT save to NVS - current program is ephemeral
  // It resets to scene's program_number on next boot/scene load
  return ESP_OK;
}

esp_err_t device_config_program_next(void) {
  if (g_device_config.bank_select_mode != BANK_SELECT_NONE) {
    // Bank mode: use preset-based logic
    // Use pending preset if one exists, otherwise current
    uint16_t base_preset = g_device_config.has_pending_program 
                           ? (g_device_config.pending_bank * 128 + g_device_config.pending_program)
                           : device_config_get_preset();
    uint16_t max_preset = device_config_get_max_preset();
    uint16_t next_preset = base_preset + 1;
    if (next_preset > max_preset) next_preset = max_preset;  // Clamp at max (no wrap)
    
    if (g_device_config.pc_mode == PC_MODE_IMMEDIATE) {
      return device_config_set_preset(next_preset);
    } else {
      g_device_config.pending_bank = (uint8_t)(next_preset / 128);
      g_device_config.pending_program = (uint8_t)(next_preset % 128);
      g_device_config.has_pending_program = true;
      ESP_LOGI(TAG, "Pending preset: bank %d, program %d (confirm to send)", 
               g_device_config.pending_bank, g_device_config.pending_program);
      return ESP_OK;
    }
  }
  
  // No bank mode: respect preset count if locked, otherwise 0-127
  // Cap at 127 since non-bank mode can't exceed that
  uint16_t max_preset = device_config_get_max_preset();
  uint8_t max_prog = (max_preset > 127) ? 127 : (uint8_t)max_preset;
  // Use pending program if one exists, otherwise current
  uint8_t base = g_device_config.has_pending_program 
                 ? g_device_config.pending_program 
                 : g_device_config.current_program;
  uint8_t next = base + 1;
  if (next > max_prog) next = max_prog;  // Clamp at max (no wrap with preset lock)
  
  if (g_device_config.pc_mode == PC_MODE_IMMEDIATE) {
    return device_config_set_program(next);
  } else {
    g_device_config.pending_program = next;
    g_device_config.has_pending_program = true;
    ESP_LOGI(TAG, "Pending program: %d (confirm to send)", next);
    return ESP_OK;
  }
}

esp_err_t device_config_program_prev(void) {
  if (g_device_config.bank_select_mode != BANK_SELECT_NONE) {
    // Bank mode: use preset-based logic
    // Use pending preset if one exists, otherwise current
    uint16_t base_preset = g_device_config.has_pending_program 
                           ? (g_device_config.pending_bank * 128 + g_device_config.pending_program)
                           : device_config_get_preset();
    uint16_t prev_preset = (base_preset > 0) ? base_preset - 1 : 0;  // Clamp at 0 (no wrap)
    
    if (g_device_config.pc_mode == PC_MODE_IMMEDIATE) {
      return device_config_set_preset(prev_preset);
    } else {
      g_device_config.pending_bank = (uint8_t)(prev_preset / 128);
      g_device_config.pending_program = (uint8_t)(prev_preset % 128);
      g_device_config.has_pending_program = true;
      ESP_LOGI(TAG, "Pending preset: bank %d, program %d (confirm to send)", 
               g_device_config.pending_bank, g_device_config.pending_program);
      return ESP_OK;
    }
  }
  
  // No bank mode: clamp at 0 (no wrap when preset lock active)
  // Use pending program if one exists, otherwise current
  uint8_t base = g_device_config.has_pending_program 
                 ? g_device_config.pending_program 
                 : g_device_config.current_program;
  uint8_t prev = (base == 0) ? 0 : base - 1;
  
  if (g_device_config.pc_mode == PC_MODE_IMMEDIATE) {
    return device_config_set_program(prev);
  } else {
    g_device_config.pending_program = prev;
    g_device_config.has_pending_program = true;
    ESP_LOGI(TAG, "Pending program: %d (confirm to send)", prev);
    return ESP_OK;
  }
}

pc_change_mode_t device_config_get_pc_mode(void) {
  return g_device_config.pc_mode;
}

esp_err_t device_config_set_pc_mode(pc_change_mode_t mode) {
  g_device_config.pc_mode = mode;
  
  ESP_LOGI(TAG, "PC mode set to %s", mode == PC_MODE_IMMEDIATE ? "immediate" : "pending");
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_PC_MODE, mode_val);
}

uint8_t device_config_get_pending_program(void) {
  return g_device_config.pending_program;
}

bool device_config_has_pending_program(void) {
  return g_device_config.has_pending_program;
}

esp_err_t device_config_set_pending_program(uint8_t program) {
  if (program > 127) {
    return ESP_ERR_INVALID_ARG;
  }
  g_device_config.pending_program = program;
  g_device_config.has_pending_program = true;
  ESP_LOGI(TAG, "Pending program: %d (confirm to send)", program);
  return ESP_OK;
}

esp_err_t device_config_confirm_program(void) {
  if (!g_device_config.has_pending_program) {
    ESP_LOGW(TAG, "No pending program change to confirm");
    return ESP_ERR_INVALID_STATE;
  }
  
  g_device_config.has_pending_program = false;
  
  // When bank mode is enabled, we need to update bank too
  if (g_device_config.bank_select_mode != BANK_SELECT_NONE) {
    g_device_config.current_bank = g_device_config.pending_bank;
  }
  
  return device_config_set_program(g_device_config.pending_program);
}

esp_err_t device_config_cancel_pending_program(void) {
  if (!g_device_config.has_pending_program) {
    ESP_LOGW(TAG, "No pending program change to cancel");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(TAG, "Cancelled pending program change to %d", g_device_config.pending_program);
  g_device_config.has_pending_program = false;
  g_device_config.pending_program = g_device_config.current_program;
  g_device_config.pending_bank = g_device_config.current_bank;
  
  return ESP_OK;
}

// Bank select management
bank_select_mode_t device_config_get_bank_mode(void) {
  return g_device_config.bank_select_mode;
}

esp_err_t device_config_set_bank_mode(bank_select_mode_t mode) {
  if (mode > BANK_SELECT_CC0_CC32) {
    ESP_LOGE(TAG, "Invalid bank select mode %d", mode);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.bank_select_mode = mode;
  
  const char* mode_str = "none";
  switch (mode) {
    case BANK_SELECT_CC0: mode_str = "CC0+PC"; break;
    case BANK_SELECT_CC0_CC32: mode_str = "CC0+CC32+PC"; break;
    default: break;
  }
  ESP_LOGI(TAG, "Bank select mode set to %s", mode_str);
  
  return app_settings_save_u8(NVS_KEY_BANK_MODE, (uint8_t)mode);
}

uint8_t device_config_get_bank(void) {
  return g_device_config.current_bank;
}

esp_err_t device_config_set_bank(uint8_t bank) {
  if (bank > 127) {
    ESP_LOGE(TAG, "Invalid bank number %d (must be 0-127)", bank);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.current_bank = bank;
  ESP_LOGI(TAG, "Bank set to %d", bank);
  return ESP_OK;
}

uint8_t device_config_get_pending_bank(void) {
  return g_device_config.pending_bank;
}

// Preset management (combines bank + program)
uint16_t device_config_get_preset(void) {
  return ((uint16_t)g_device_config.current_bank * 128) + g_device_config.current_program;
}

esp_err_t device_config_set_preset(uint16_t preset) {
  uint16_t max_preset = device_config_get_max_preset();
  
  if (preset > max_preset) {
    ESP_LOGD(TAG, "Preset %u exceeds max %u, clamping", (unsigned)preset, (unsigned)max_preset);
    preset = max_preset;
  }
  
  g_device_config.current_bank = (uint8_t)(preset / 128);
  g_device_config.current_program = (uint8_t)(preset % 128);
  
  // Send MIDI program change with bank
  uint8_t channel = g_device_config.midi_channel - 1;
  send_program_with_bank(channel, g_device_config.current_bank, g_device_config.current_program);
  
  // Display preset as 1-based for user (internal preset 0 = display preset 1)
  ESP_LOGI(TAG, "Preset %u (bank %d, PC %d) on channel %d",
           (unsigned)(preset + 1), g_device_config.current_bank, 
           g_device_config.current_program, g_device_config.midi_channel);
  
  return ESP_OK;
}

uint16_t device_config_get_pending_preset(void) {
  return ((uint16_t)g_device_config.pending_bank * 128) + g_device_config.pending_program;
}

esp_err_t device_config_set_pending_preset(uint16_t preset) {
  if (preset > 16383) {
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.pending_bank = (uint8_t)(preset / 128);
  g_device_config.pending_program = (uint8_t)(preset % 128);
  g_device_config.has_pending_program = true;
  
  ESP_LOGI(TAG, "Pending preset: %u (bank %d, program %d)", 
           (unsigned)preset, g_device_config.pending_bank, g_device_config.pending_program);
  return ESP_OK;
}

uint8_t device_config_get_preset_base(void) {
  return g_device_config.preset_base;
}

esp_err_t device_config_set_preset_base(uint8_t base) {
  if (base > 1) {
    ESP_LOGE(TAG, "Invalid preset_base %d (must be 0 or 1)", base);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.preset_base = base;
  app_settings_save_u8("preset_base", base);
  
  ESP_LOGI(TAG, "Preset base set to %d (presets display as %d-based)", base, base);
  return ESP_OK;
}

uint16_t device_config_get_preset_count(void) {
  return g_device_config.preset_count;
}

esp_err_t device_config_set_preset_count(uint16_t count) {
  if (count == 0 || count > 16384) {
    ESP_LOGE(TAG, "Invalid preset count %u (must be 1-16384)", (unsigned)count);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.preset_count = count;
  ESP_LOGI(TAG, "Preset count set to %u", (unsigned)count);
  return ESP_OK;
}

bool device_config_get_lock_preset_range(void) {
  return g_device_config.lock_preset_range;
}

esp_err_t device_config_set_lock_preset_range(bool lock) {
  g_device_config.lock_preset_range = lock;
  ESP_LOGI(TAG, "Preset range lock: %s", lock ? "enabled" : "disabled");
  return app_settings_save_u8(NVS_KEY_PRESET_LOCK, lock ? 1 : 0);
}

uint16_t device_config_get_max_preset(void) {
  // Always respect the device's preset count
  // Lock setting only controls clamp vs wrap, not the range itself
  if (g_device_config.preset_count > 0) {
    return g_device_config.preset_count - 1;
  }
  // Fallback if no preset count defined
  return (g_device_config.bank_select_mode != BANK_SELECT_NONE) ? 16383 : 127;
}


