#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "midi_messages.h"

// Maximum number of scenes
#define MAX_SCENES 128

// Number of touchpads
#define NUM_TOUCHPADS 12

// Touchwheel consists of pads 0-7
#define TOUCHWHEEL_START 0
#define TOUCHWHEEL_END 7
#define TOUCHWHEEL_SIZE 8

// Scene operational modes
typedef enum {
  SCENE_MODE_SINGLE,        // Mode 1: Single scene, PC messages available
  SCENE_MODE_PRESET_SYNC,   // Mode 2: 1:1 scene-to-preset mapping
  SCENE_MODE_ADVANCED       // Mode 3: Arbitrary PC messages per scene
} scene_mode_t;

// Scene change modes
typedef enum {
  CHANGE_MODE_IMMEDIATE,    // Send PC and change immediately
  CHANGE_MODE_PENDING       // Show pending, confirm with separate action
} scene_change_mode_t;

// Touchwheel behavior modes
typedef enum {
  TOUCHWHEEL_MODE_BUTTONS,    // Each pad acts as individual button
  TOUCHWHEEL_MODE_ENCODER     // Wheel acts as increment/decrement control
} touchwheel_mode_t;

// Control change assignment types
typedef enum {
  CC_TYPE_SIMPLE,             // Single CC, single value
  CC_TYPE_TOGGLE,             // Toggle between two values
  CC_TYPE_MULTI_STATE,        // Cycle through multiple values
  CC_TYPE_CONTINUOUS          // Continuous value (for sensors/expression)
} cc_assignment_type_t;

// Simple CC assignment for initial implementation
typedef struct {
  uint8_t cc_number;          // MIDI CC number (0-127)
  uint8_t value;              // Value to send (0-127)
  uint8_t channel;            // MIDI channel (1-16, 0 = inherit from scene)
} cc_assignment_t;

// Touchpad mapping
typedef struct {
  bool enabled;               // Whether this touchpad is active
  cc_assignment_t cc;         // CC assignment (for now, just simple)
  // Future: union of different assignment types
} touchpad_mapping_t;

// Scene structure
typedef struct {
  char name[32];              // Scene name
  
  // Program change settings (modes 2 & 3)
  uint8_t program_number;     // PC value (0-127)
  bool send_pc_on_change;     // Send PC when switching to this scene
  
  // Touchwheel configuration
  touchwheel_mode_t touchwheel_mode;
  cc_assignment_t touchwheel_cc;  // Used when in encoder mode
  
  // Touchpad mappings (0-11 only, pad 12 is UI button)
  touchpad_mapping_t touchpads[NUM_TOUCHPADS];
  
  // Future: other input mappings (expression, CV, sensors)
} scene_t;

// Scene manager state
typedef struct {
  scene_t scenes[MAX_SCENES];
  uint8_t current_scene_index;
  uint8_t pending_scene_index;  // For pending change mode
  bool has_pending_change;
  uint8_t num_scenes;           // Number of initialized scenes
  scene_mode_t mode;
  scene_change_mode_t change_mode;
  bool initialized;
} scene_manager_t;

// Initialize the scene manager
esp_err_t scene_init(void);

// Scene navigation
esp_err_t scene_set_current(uint8_t scene_index);
uint8_t scene_get_current_index(void);
scene_t* scene_get_current(void);
esp_err_t scene_next(void);
esp_err_t scene_previous(void);

// Scene mode configuration
esp_err_t scene_set_mode(scene_mode_t mode);
scene_mode_t scene_get_mode(void);
esp_err_t scene_set_change_mode(scene_change_mode_t mode);
scene_change_mode_t scene_get_change_mode(void);

// Scene configuration
esp_err_t scene_set_name(uint8_t scene_index, const char* name);
esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode);
esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program);
esp_err_t scene_set_send_pc(uint8_t scene_index, bool send_pc);

// Touchpad mapping configuration
esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, 
                                uint8_t cc_number, uint8_t value, uint8_t channel);
esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled);
touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index);

// Pending change mode
uint8_t scene_get_pending_index(void);
bool scene_has_pending_change(void);
esp_err_t scene_confirm_change(void);
esp_err_t scene_cancel_pending(void);

// Process touchpad events through scene mappings
esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed);

// Utility functions
uint8_t scene_get_effective_channel(const touchpad_mapping_t* mapping, const scene_t* scene);

// Save/load scene configuration to/from NVS
esp_err_t scene_save_config(void);
esp_err_t scene_load_config(void);

#endif // SCENE_H


