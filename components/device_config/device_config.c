#include "device_config.h"
#include "app_settings.h"
#include "midi_messages.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "device_config";

// NVS keys
#define NVS_KEY_DEVICE_MODE     "dev_mode"
#define NVS_KEY_MIDI_CHANNEL    "dev_channel"
#define NVS_KEY_TRS_TYPE        "dev_trs"
#define NVS_KEY_PEDAL_SLUG      "dev_slug"
#define NVS_KEY_CUSTOM_NAME     "dev_custom"
#define NVS_KEY_CURRENT_PROGRAM "dev_program"
#define NVS_KEY_PC_MODE         "dev_pc_mode"

// Global device configuration
static device_config_t g_device_config = {
  .mode = DEVICE_MODE_CUSTOM,
  .midi_channel = 1,
  .trs_type = MIDI_TRS_TYPE_BOTH,
  .pedal_slug = "",
  .custom_name = "Generic MIDI Device",
  .current_program = 0,
  .pending_program = 0,
  .has_pending_program = false,
  .pc_mode = PC_MODE_IMMEDIATE,
  .initialized = false
};

esp_err_t device_config_init(void) {
  if (g_device_config.initialized) {
    ESP_LOGW(TAG, "Device config already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing device configuration");
  
  // Try to load from NVS
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_DEVICE_MODE, &mode_val) == ESP_OK) {
    g_device_config.mode = (device_mode_t)mode_val;
  }
  
  uint8_t channel;
  if (app_settings_load_u8(NVS_KEY_MIDI_CHANNEL, &channel) == ESP_OK && channel >= 1 && channel <= 16) {
    g_device_config.midi_channel = channel;
  }
  
  uint8_t trs_val;
  if (app_settings_load_u8(NVS_KEY_TRS_TYPE, &trs_val) == ESP_OK) {
    g_device_config.trs_type = (midi_trs_type_t)trs_val;
  }
  
  char slug[64];
  if (app_settings_load_str(NVS_KEY_PEDAL_SLUG, slug, sizeof(slug)) == ESP_OK) {
    strncpy(g_device_config.pedal_slug, slug, sizeof(g_device_config.pedal_slug) - 1);
    g_device_config.pedal_slug[sizeof(g_device_config.pedal_slug) - 1] = '\0';
  }
  
  char custom[64];
  if (app_settings_load_str(NVS_KEY_CUSTOM_NAME, custom, sizeof(custom)) == ESP_OK) {
    strncpy(g_device_config.custom_name, custom, sizeof(g_device_config.custom_name) - 1);
    g_device_config.custom_name[sizeof(g_device_config.custom_name) - 1] = '\0';
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
  
  g_device_config.initialized = true;
  
  ESP_LOGI(TAG, "Device config initialized: mode=%s, channel=%d, trs=%d",
           g_device_config.mode == DEVICE_MODE_DATABASE ? "database" : "custom",
           g_device_config.midi_channel,
           g_device_config.trs_type);
  
  if (g_device_config.mode == DEVICE_MODE_DATABASE) {
    ESP_LOGI(TAG, "  Pedal: %s", g_device_config.pedal_slug);
  } else {
    ESP_LOGI(TAG, "  Custom: %s", g_device_config.custom_name);
  }
  
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
  ESP_LOGI(TAG, "MIDI channel set to %d", channel);
  
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
    case MIDI_TRS_TYPE_CS: type_str = "Type CS (two-wire)"; break;
    case MIDI_TRS_TYPE_BOTH: type_str = "Both (Type A + Type B)"; break;
    default: break;
  }
  
  ESP_LOGI(TAG, "TRS wiring type set to %s", type_str);
  
  uint8_t type_val = (uint8_t)type;
  return app_settings_save_u8(NVS_KEY_TRS_TYPE, type_val);
}

device_mode_t device_config_get_mode(void) {
  return g_device_config.mode;
}

esp_err_t device_config_set_pedal(const char* slug) {
  if (!slug) {
    return ESP_ERR_INVALID_ARG;
  }
  
  strncpy(g_device_config.pedal_slug, slug, sizeof(g_device_config.pedal_slug) - 1);
  g_device_config.pedal_slug[sizeof(g_device_config.pedal_slug) - 1] = '\0';
  g_device_config.mode = DEVICE_MODE_DATABASE;
  
  ESP_LOGI(TAG, "Device set to database pedal: %s", slug);
  
  // Save both mode and slug
  uint8_t mode_val = (uint8_t)g_device_config.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_DEVICE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  return app_settings_save_str(NVS_KEY_PEDAL_SLUG, g_device_config.pedal_slug);
}

esp_err_t device_config_set_custom(const char* name) {
  if (!name) {
    return ESP_ERR_INVALID_ARG;
  }
  
  strncpy(g_device_config.custom_name, name, sizeof(g_device_config.custom_name) - 1);
  g_device_config.custom_name[sizeof(g_device_config.custom_name) - 1] = '\0';
  g_device_config.mode = DEVICE_MODE_CUSTOM;
  
  ESP_LOGI(TAG, "Device set to custom: %s", name);
  
  // Save both mode and custom name
  uint8_t mode_val = (uint8_t)g_device_config.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_DEVICE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  return app_settings_save_str(NVS_KEY_CUSTOM_NAME, g_device_config.custom_name);
}

const char* device_config_get_pedal_slug(void) {
  return g_device_config.mode == DEVICE_MODE_DATABASE ? g_device_config.pedal_slug : NULL;
}

const char* device_config_get_custom_name(void) {
  return g_device_config.mode == DEVICE_MODE_CUSTOM ? g_device_config.custom_name : NULL;
}

const device_config_t* device_config_get(void) {
  return &g_device_config;
}

esp_err_t device_config_save(void) {
  ESP_LOGI(TAG, "Saving device configuration to NVS");
  
  uint8_t mode_val = (uint8_t)g_device_config.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_DEVICE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  ret = app_settings_save_u8(NVS_KEY_MIDI_CHANNEL, g_device_config.midi_channel);
  if (ret != ESP_OK) return ret;
  
  uint8_t trs_val = (uint8_t)g_device_config.trs_type;
  ret = app_settings_save_u8(NVS_KEY_TRS_TYPE, trs_val);
  if (ret != ESP_OK) return ret;
  
  if (g_device_config.mode == DEVICE_MODE_DATABASE) {
    ret = app_settings_save_str(NVS_KEY_PEDAL_SLUG, g_device_config.pedal_slug);
  } else {
    ret = app_settings_save_str(NVS_KEY_CUSTOM_NAME, g_device_config.custom_name);
  }
  
  return ret;
}

uint8_t device_config_get_program(void) {
  return g_device_config.current_program;
}

esp_err_t device_config_set_program(uint8_t program) {
  if (program > 127) {
    ESP_LOGE(TAG, "Invalid program number %d (must be 0-127)", program);
    return ESP_ERR_INVALID_ARG;
  }
  
  g_device_config.current_program = program;
  
  // Send MIDI program change
  send_program_change(g_device_config.midi_channel - 1, program);
  ESP_LOGI(TAG, "Program changed to %d (sent on channel %d)", program, g_device_config.midi_channel);
  
  return app_settings_save_u8(NVS_KEY_CURRENT_PROGRAM, program);
}

esp_err_t device_config_program_next(void) {
  uint8_t next = g_device_config.current_program + 1;
  if (next > 127) next = 0;  // Wrap around
  
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
  uint8_t prev = (g_device_config.current_program == 0) ? 127 : g_device_config.current_program - 1;
  
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

esp_err_t device_config_confirm_program(void) {
  if (!g_device_config.has_pending_program) {
    ESP_LOGW(TAG, "No pending program change to confirm");
    return ESP_ERR_INVALID_STATE;
  }
  
  g_device_config.has_pending_program = false;
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
  
  return ESP_OK;
}


