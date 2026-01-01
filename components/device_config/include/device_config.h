#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "assets_types.h"

// Program change modes
typedef enum {
  PC_MODE_IMMEDIATE,    // Send PC immediately on button press
  PC_MODE_PENDING       // Show pending, send on confirmation
} pc_change_mode_t;

// Bank select modes for program changes
typedef enum {
  BANK_SELECT_NONE = 0,   // PC only (default, 0-127)
  BANK_SELECT_CC0,        // CC0 + PC (128 banks × 128 programs = 16384 presets)
  BANK_SELECT_CC0_CC32    // CC0 + CC32 + PC (same range, explicit LSB)
} bank_select_mode_t;

// Device configuration state
typedef struct {
  uint8_t midi_channel;        // MIDI channel (1-16)
  midi_trs_type_t trs_type;    // TRS wiring type
  char pedal_slug[64];         // Slug of device from database
  
  // Program change tracking
  uint8_t current_program;     // Current program/preset (0-127)
  uint8_t pending_program;     // Pending program (for PC_MODE_PENDING)
  bool has_pending_program;    // Whether there's a pending PC
  pc_change_mode_t pc_mode;    // Immediate or pending PC changes
  
  // Bank select support
  bank_select_mode_t bank_select_mode;  // Bank selection protocol
  uint8_t current_bank;        // Current bank (CC0 value, 0-127)
  uint8_t pending_bank;        // Pending bank (for PC_MODE_PENDING)
  
  // Display offset for program numbers (0 or 1)
  // 0: presets display as 0-127 (what MIDI sends)
  // 1: presets display as 1-128 (user-friendly, PC 0 shows as "1")
  uint8_t preset_base;
  
  // Preset range limiting
  uint16_t preset_count;         // Number of presets (from device JSON, default 128)
  bool preset_wrap;              // If true, wrap around at boundaries; if false, clamp
  
  // MIDI clock output
  bool send_clock;               // Whether to send MIDI clock to this device
  
  bool initialized;
} device_config_t;

// Initialize the device configuration
// Loads settings from NVS or sets defaults
esp_err_t device_config_init(void);

// Get the current MIDI channel (1-16)
uint8_t device_config_get_channel(void);

// Set MIDI channel (1-16)
esp_err_t device_config_set_channel(uint8_t channel);

// Get the TRS wiring type
midi_trs_type_t device_config_get_trs_type(void);

// Set TRS wiring type
esp_err_t device_config_set_trs_type(midi_trs_type_t type);

// Set device to use pedal from database
// slug: the pedal identifier from assets database (e.g., "chase_bliss.mood_mkii@0")
esp_err_t device_config_set_pedal(const char* slug);

// Get the pedal slug
const char* device_config_get_pedal_slug(void);

// Get pointer to full config structure (read-only)
const device_config_t* device_config_get(void);

// Save current configuration to NVS
esp_err_t device_config_save(void);

// Program change management
uint8_t device_config_get_program(void);
esp_err_t device_config_set_program(uint8_t program);
esp_err_t device_config_program_next(void);
esp_err_t device_config_program_prev(void);
pc_change_mode_t device_config_get_pc_mode(void);
esp_err_t device_config_set_pc_mode(pc_change_mode_t mode);
uint8_t device_config_get_pending_program(void);
bool device_config_has_pending_program(void);
esp_err_t device_config_set_pending_program(uint8_t program);
esp_err_t device_config_confirm_program(void);
esp_err_t device_config_cancel_pending_program(void);

// Bank select management
bank_select_mode_t device_config_get_bank_mode(void);
esp_err_t device_config_set_bank_mode(bank_select_mode_t mode);
uint8_t device_config_get_bank(void);
esp_err_t device_config_set_bank(uint8_t bank);
uint8_t device_config_get_pending_bank(void);

// Preset management (combines bank + program)
// preset = (bank * 128) + program, range 0-16383
uint16_t device_config_get_preset(void);
esp_err_t device_config_set_preset(uint16_t preset);
uint16_t device_config_get_pending_preset(void);
esp_err_t device_config_set_pending_preset(uint16_t preset);

// Preset display base (0 or 1)
// For UI display: displayed_number = program + preset_base
uint8_t device_config_get_preset_base(void);
esp_err_t device_config_set_preset_base(uint8_t base);

// Preset range limiting
uint16_t device_config_get_preset_count(void);
esp_err_t device_config_set_preset_count(uint16_t count);
bool device_config_get_preset_wrap(void);
esp_err_t device_config_set_preset_wrap(bool wrap);
uint16_t device_config_get_max_preset(void);  // Returns preset_count-1

// MIDI clock output control
bool device_config_get_send_clock(void);
esp_err_t device_config_set_send_clock(bool send);

#endif // DEVICE_CONFIG_H

