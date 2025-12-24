#ifndef ACTION_H
#define ACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Action types - all assignable functions
typedef enum {
  ACTION_NONE = 0,
  
  // Program/Scene control
  ACTION_PROGRAM_NEXT,
  ACTION_PROGRAM_PREV,
  ACTION_PROGRAM_SET,         // Smart PC: 0-127 or 0-16383 based on bank_select_mode
  ACTION_SCENE_NEXT,
  ACTION_SCENE_PREV,
  ACTION_SCENE_SET,           // Jump to specific scene (1-128, user-facing)
  
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
  
  // Direct MIDI output
  ACTION_SEND_CC,             // Send CC with value (on press only)
  ACTION_SEND_CC_HOLD,        // Send value1 on press, value2 on release
  ACTION_SEND_CC_CYCLE,       // Cycle through multiple CC values
  ACTION_SEND_NOTE_ON,
  ACTION_SEND_NOTE_OFF,
  
  // Randomization
  ACTION_RANDOMIZE_CC,        // Randomize one or more CCs (uses multi_random params)
  
  // System
  ACTION_CONFIRM_PENDING,     // Confirm pending scene/program change
  ACTION_RESET,               // Send CC123 + CC120 + System Reset (0xFF)
  
  // Musical concepts (assignable to any input)
  ACTION_SUSTAIN,             // Send CC64 (127 on press, 0 on release)
  ACTION_SOSTENUTO,           // Send CC66 (127 on press, 0 on release)
  
  // Touchwheel mode control
  ACTION_TOUCHWHEEL_MODE,       // Set touchwheel mode to specific value
  ACTION_TOUCHWHEEL_MODE_HOLD,  // Set mode on press, restore on release
  ACTION_TOUCHWHEEL_MODE_CYCLE, // Cycle through touchwheel modes
  
  ACTION_MAX
} action_type_t;

// Action parameters (flexible union for different action types)
typedef struct {
  action_type_t type;
  
  union {
    // For CC actions (SEND_CC, HOLD, CYCLE) - supports 1-4 CC numbers
    struct {
      uint8_t num_ccs;            // 1-4 CC numbers (1 = single CC mode)
      uint8_t cc_numbers[4];      // The CC numbers
      uint8_t values[4];          // Primary values for SEND_CC (one per CC)
      uint8_t values2[4];         // Release values for SEND_CC_HOLD (one per CC)
      uint8_t num_cycle_steps;    // For cycle: 2-8 steps (shared across all CCs)
      uint8_t cycle_values[4][8]; // For cycle: [cc_idx][step]
      uint8_t current_index;      // Current position in cycle (shared)
    } cc;
    
    // For note actions
    struct {
      uint8_t note;
      uint8_t velocity;
    } note;
    
    // For scene set (0-127 range)
    struct {
      uint8_t number;
    } target;
    
    // For program set (0-127 or 0-16383 based on bank mode)
    struct {
      uint16_t program;
    } pc;
    
    // For tempo actions
    struct {
      uint16_t bpm;           // For set_tempo (20-300)
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
action_t action_create_send_cc(uint8_t cc_number, uint8_t value);
action_t action_create_cc_hold(uint8_t cc_number, uint8_t press_value, uint8_t release_value);
action_t action_create_program_next(void);
action_t action_create_program_prev(void);
action_t action_create_scene_next(void);
action_t action_create_scene_prev(void);
action_t action_create_tap(void);
action_t action_create_tap_tempo(void);
action_t action_create_set_tempo(uint16_t bpm);
action_t action_create_transport(action_type_t transport_type);
action_t action_create_reset(void);
action_t action_create_sustain(void);
action_t action_create_sostenuto(void);
action_t action_create_touchwheel_mode(uint8_t mode);
action_t action_create_touchwheel_mode_hold(uint8_t press_mode, uint8_t release_mode);

// Get action type name (for debugging/console)
const char* action_type_to_string(action_type_t type);

// Check if action type requires hold (press/release) behavior
// These actions should NOT be assigned to bump or on_load
bool action_requires_hold(action_type_t type);

#endif // ACTION_H
