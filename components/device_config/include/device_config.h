#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "assets_types.h"

// Device configuration mode
typedef enum {
  DEVICE_MODE_DATABASE,  // Using pedal from assets database
  DEVICE_MODE_CUSTOM     // User-defined custom device
} device_mode_t;

// Program change modes
typedef enum {
  PC_MODE_IMMEDIATE,    // Send PC immediately on button press
  PC_MODE_PENDING       // Show pending, send on confirmation
} pc_change_mode_t;

// Device configuration state
typedef struct {
  device_mode_t mode;          // Database or custom
  uint8_t midi_channel;        // MIDI channel (1-16)
  midi_trs_type_t trs_type;    // TRS wiring type
  
  // Database mode
  char pedal_slug[64];         // Slug of device from database
  
  // Custom mode
  char custom_name[64];        // User-defined device name
  
  // Program change tracking
  uint8_t current_program;     // Current program/preset (0-127)
  uint8_t pending_program;     // Pending program (for PC_MODE_PENDING)
  bool has_pending_program;    // Whether there's a pending PC
  pc_change_mode_t pc_mode;    // Immediate or pending PC changes
  
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

// Get the current device mode
device_mode_t device_config_get_mode(void);

// Set device to use pedal from database
// slug: the pedal identifier from assets database
esp_err_t device_config_set_pedal(const char* slug);

// Set device to custom user-defined configuration
// name: user-defined device name
esp_err_t device_config_set_custom(const char* name);

// Get the pedal slug (valid when mode is DEVICE_MODE_DATABASE)
const char* device_config_get_pedal_slug(void);

// Get the custom device name (valid when mode is DEVICE_MODE_CUSTOM)
const char* device_config_get_custom_name(void);

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

#endif // DEVICE_CONFIG_H

