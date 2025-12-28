#ifndef ACTION_H
#define ACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Action types - all assignable functions
typedef enum {
  ACTION_NONE = 0,
  
  // Preset/Scene control
  ACTION_PRESET_INC,
  ACTION_PRESET_DEC,
  ACTION_PRESET,              // Smart PC: 0-127 or 0-16383 based on bank_select_mode
  ACTION_PRESET_HOLD,         // Press: set one preset, Release: set another
  ACTION_PRESET_CYCLE,        // Cycle through presets on each press
  ACTION_SCENE_INC,
  ACTION_SCENE_DEC,
  ACTION_SCENE,               // Jump to specific scene (1-128, user-facing)
  
  // Transport (Play and Record are toggles)
  ACTION_PLAY,                // Toggle: playing -> stop, else -> play
  ACTION_STOP,
  ACTION_PAUSE,               // Pause only (does not unpause)
  ACTION_RECORD,              // Toggle: recording -> stop, else -> record
  
  // Tempo
  ACTION_TAP,                 // Send a single tap input (for tap tempo)
  ACTION_TAP_TEMPO,           // Toggle/start/stop tap tempo session
  ACTION_SET_TEMPO,           // Set BPM directly (uses tempo.bpm param)
  ACTION_TEMPO_INC,           // Increment BPM by 1
  ACTION_TEMPO_DEC,           // Decrement BPM by 1
  ACTION_TEMPO_HOLD,          // Set tempo on press, different tempo on release
  ACTION_TEMPO_CYCLE,         // Cycle through tempo values on each press
  
  // Direct MIDI output
  ACTION_CONTROL,             // Send CC with value (on press only)
  ACTION_CONTROL_HOLD,        // Send value1 on press, value2 on release
  ACTION_CONTROL_CYCLE,       // Cycle through multiple CC values
  ACTION_NOTE,                // Send Note On on press, Note Off on release
  
  // Randomization
  ACTION_RANDOMIZE,           // Randomize one or more CCs (uses multi_random params)
  
  // System
  ACTION_CONFIRM_PENDING,     // Confirm pending scene/program change
  ACTION_RESET,               // Send CC123 + CC120 + System Reset (0xFF)
  
  // Musical concepts (assignable to any input)
  ACTION_SUSTAIN,             // Send CC64 (127 on press, 0 on release)
  ACTION_SOSTENUTO,           // Send CC66 (127 on press, 0 on release)
  
  // Touchwheel mode control
  ACTION_TOUCHWHEEL_MODE,     // Set touchwheel mode to specific value
  ACTION_TOUCHWHEEL_HOLD,     // Set mode on press, restore on release
  ACTION_TOUCHWHEEL_CYCLE,    // Cycle through touchwheel modes
  
  ACTION_MAX
} action_type_t;

// Action parameters (flexible union for different action types)
typedef struct {
  action_type_t type;
  
  union {
    // For Control actions (CONTROL, CONTROL_HOLD, CONTROL_CYCLE) - supports 1-4 CC numbers
    struct {
      uint8_t num_ccs;            // 1-4 CC numbers (1 = single CC mode)
      uint8_t cc_numbers[4];      // The CC numbers
      uint8_t values[4];          // Primary values for CONTROL (one per CC)
      uint8_t values2[4];         // Release values for CONTROL_HOLD (one per CC)
      uint8_t num_cycle_steps;    // For cycle: 2-8 steps (shared across all CCs)
      uint8_t cycle_values[4][8]; // For cycle: [cc_idx][step]
      uint8_t current_index;      // Current position in cycle (shared)
    } control;
    
    // For note actions (hold-style: press=on, release=off)
    struct {
      uint8_t note;
      uint8_t velocity;
    } note;
    
    // For scene set (0-127 range)
    struct {
      uint8_t number;
    } target;
    
    // For preset set (0-127 or 0-16383 based on bank mode)
    struct {
      uint16_t program;
    } preset;
    
    // For preset hold/cycle
    struct {
      uint16_t press_preset;      // Preset to set on press (hold)
      uint16_t release_preset;    // Preset to set on release (hold)
      uint8_t num_presets;        // Number of presets in cycle (2-8)
      uint16_t cycle_presets[8];  // Preset values for cycle
      uint8_t current_index;      // Current position in cycle
    } preset_cycle;
    
    // For tempo actions (set, hold, cycle)
    struct {
      uint16_t bpm;               // For set_tempo (20-300)
      uint16_t press_bpm;         // For hold: tempo on press
      uint16_t release_bpm;       // For hold: tempo on release
      uint8_t num_tempos;         // For cycle: number of tempos (2-8)
      uint16_t cycle_tempos[8];   // For cycle: tempo values
      uint8_t current_index;      // Current position in cycle
    } tempo;
    
    // For randomize (one or more CCs, always 0-127 range)
    struct {
      uint8_t num_ccs;
      uint8_t cc_numbers[8];
    } randomize;
    
    // For touchwheel mode actions
    struct {
      uint8_t mode;           // Primary mode (touchwheel_mode_t)
      uint8_t mode2;          // For hold: release mode
      uint8_t num_modes;      // For cycle: number of modes (2-8)
      uint8_t modes[8];       // For cycle: mode values
      uint8_t current_index;  // Current position in cycle
    } tw_mode;
  } params;
} action_t;

// Maximum on_load actions per scene
#define MAX_ON_LOAD_ACTIONS 4

// Initialize the action system
esp_err_t action_init(void);

// Execute a single action
// trigger_value: for discrete inputs this is boolean (0/1), for continuous it's the value (0-127)
// is_press: true for press/on, false for release/off (only for discrete inputs)
esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press);

// Helper functions to create common actions
action_t action_create_control(uint8_t cc_number, uint8_t value);
action_t action_create_control_hold(uint8_t cc_number, uint8_t press_value, uint8_t release_value);
action_t action_create_preset_inc(void);
action_t action_create_preset_dec(void);
action_t action_create_scene_inc(void);
action_t action_create_scene_dec(void);
action_t action_create_tap(void);
action_t action_create_tap_tempo(void);
action_t action_create_set_tempo(uint16_t bpm);
action_t action_create_transport(action_type_t transport_type);
action_t action_create_reset(void);
action_t action_create_sustain(void);
action_t action_create_sostenuto(void);
action_t action_create_touchwheel_mode(uint8_t mode);
action_t action_create_touchwheel_hold(uint8_t press_mode, uint8_t release_mode);

// Get action type name (for debugging/console)
const char* action_type_to_string(action_type_t type);

// Check if action type requires hold (press/release) behavior
// These actions should NOT be assigned to bump or on_load
bool action_requires_hold(action_type_t type);

#endif // ACTION_H
