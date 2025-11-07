#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "midi_messages.h"
#include "action.h"
#include "continuous_mapping.h"

// Scene cache size - we keep current + prev + next in RAM
#define SCENE_CACHE_SIZE 3
#define MAX_SCENE_INDEX 127  // Maximum scene number (0-127)

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

// Touchpad mapping
typedef struct {
  bool enabled;               // Whether this touchpad is active
  action_chain_t actions;     // Action chain to execute
} touchpad_mapping_t;

// Scene structure
typedef struct {
  char name[32];              // Scene name
  
  // Program change settings (modes 2 & 3)
  uint8_t program_number;     // PC value (0-127)
  bool send_pc_on_load;       // Send PC when loading this scene
  
  // Scene load actions
  action_chain_t on_load;     // Actions to execute when scene loads (e.g., initialize pedal state)
  
  // Touchwheel configuration
  touchwheel_mode_t touchwheel_mode;
  action_chain_t touchwheel_actions;  // Used when in encoder mode
  
  // Discrete input assignments (action chains)
  touchpad_mapping_t touchpads[NUM_TOUCHPADS];
  action_chain_t button_left;
  action_chain_t button_right;
  action_chain_t button_both;
  action_chain_t bump;               // Bump detector (one-shot trigger)
  
  // Continuous input mappings
  continuous_mapping_t expression;
  continuous_mapping_t cv;
  continuous_mapping_t proximity;
  continuous_mapping_t als;          // Ambient light sensor
  
  // Future: sustain, sostenuto, envelope follower, LFO slots
} scene_t;

// Scene cache entry
typedef struct {
  scene_t scene;
  uint8_t index;        // Which scene this is (0-127)
  bool valid;           // Whether this cache entry contains valid data
  bool dirty;           // Whether scene needs to be saved to flash
} scene_cache_entry_t;

// Scene manifest entry (lightweight metadata)
typedef struct {
  uint8_t index;
  char name[32];
  char filename[64];
} scene_manifest_entry_t;

// Scene manager state
typedef struct {
  // Scene cache (current + neighbors for fast navigation)
  scene_cache_entry_t cache[SCENE_CACHE_SIZE];
  int current_cache_idx;        // Which cache slot is current scene
  
  uint8_t current_scene_index;  // Current scene number (0-127)
  uint8_t pending_scene_index;  // For pending change mode
  bool has_pending_change;
  
  // Scene manifest
  scene_manifest_entry_t* manifest;  // Dynamic array of scene metadata
  uint16_t num_scenes;               // Total number of scenes in manifest
  
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
esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc);

// Touchpad configuration
esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, 
                                uint8_t cc_number, uint8_t value);
esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled);
touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index);

// Action-based assignment API
esp_err_t scene_assign_touchpad_action(uint8_t scene_index, uint8_t pad_index, const action_t* action);
esp_err_t scene_assign_touchpad_chain(uint8_t scene_index, uint8_t pad_index, const action_chain_t* chain);
esp_err_t scene_assign_button_left(uint8_t scene_index, const action_chain_t* chain);
esp_err_t scene_assign_button_right(uint8_t scene_index, const action_chain_t* chain);
esp_err_t scene_assign_button_both(uint8_t scene_index, const action_chain_t* chain);
esp_err_t scene_assign_on_load(uint8_t scene_index, const action_chain_t* chain);
action_chain_t* scene_get_button_left(uint8_t scene_index);
action_chain_t* scene_get_button_right(uint8_t scene_index);
action_chain_t* scene_get_button_both(uint8_t scene_index);
action_chain_t* scene_get_on_load(uint8_t scene_index);

// Pending change mode
uint8_t scene_get_pending_index(void);
bool scene_has_pending_change(void);
esp_err_t scene_confirm_change(void);
esp_err_t scene_cancel_pending(void);

// Process touchpad events through scene mappings
esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed);


// Save/load scene mode configuration to/from NVS
esp_err_t scene_save_config(void);
esp_err_t scene_load_config(void);

// Scene storage (flash-based)
esp_err_t scene_load_from_flash(uint8_t scene_index);
esp_err_t scene_save_to_flash(uint8_t scene_index);
esp_err_t scene_load_manifest(void);
esp_err_t scene_save_manifest(void);
esp_err_t scene_create_new(const char* name);
esp_err_t scene_delete(uint8_t scene_index);
esp_err_t scene_duplicate(uint8_t source_index, const char* new_name);
esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index);
uint16_t scene_get_count(void);

#endif // SCENE_H


