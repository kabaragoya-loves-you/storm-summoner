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
  ACTION_PROGRAM_SET,         // Jump to specific program
  ACTION_SCENE_NEXT,
  ACTION_SCENE_PREV,
  ACTION_SCENE_SET,           // Jump to specific scene
  
  // Transport
  ACTION_TRANSPORT_PLAY,
  ACTION_TRANSPORT_STOP,
  ACTION_TRANSPORT_PAUSE,
  ACTION_TRANSPORT_RECORD,
  ACTION_TRANSPORT_TOGGLE,
  
  // Tempo
  ACTION_TAP_TEMPO,
  ACTION_TEMPO_NUDGE_UP,
  ACTION_TEMPO_NUDGE_DOWN,
  
  // Direct MIDI output
  ACTION_SEND_CC,             // Send CC with value
  ACTION_SEND_CC_TOGGLE,      // Toggle between two CC values
  ACTION_SEND_CC_CYCLE,       // Cycle through multiple CC values
  ACTION_SEND_NOTE_ON,
  ACTION_SEND_NOTE_OFF,
  ACTION_SEND_PC,
  
  // Randomization
  ACTION_RANDOMIZE_CC,        // Randomize single CC
  ACTION_RANDOMIZE_MULTI,     // Randomize multiple CCs
  
  // System
  ACTION_SCREENSAVER_TOGGLE,
  ACTION_CONFIRM_PENDING,     // Confirm pending scene/program change
  ACTION_CANCEL_PENDING,      // Cancel pending change
  ACTION_ALL_NOTES_OFF,       // Send CC123 (All Notes Off)
  ACTION_ALL_SOUND_OFF,       // Send CC120 (All Sound Off)
  
  ACTION_MAX
} action_type_t;

// Action parameters (flexible union for different action types)
typedef struct {
  action_type_t type;
  
  union {
    // For CC actions (SEND_CC, TOGGLE, CYCLE, RANDOMIZE)
    struct {
      uint8_t cc_number;
      uint8_t value;          // Primary value
      uint8_t value2;         // For toggle (second value)
      uint8_t num_values;     // For cycle (2-8 values)
      uint8_t values[8];      // For cycle
      uint8_t current_index;  // Current position in cycle
    } cc;
    
    // For note actions
    struct {
      uint8_t note;
      uint8_t velocity;
    } note;
    
    // For target actions (program/scene set)
    struct {
      uint8_t number;
    } target;
    
    // For tempo actions
    struct {
      uint8_t bpm_delta;      // For nudge (default 1)
    } tempo;
    
    // For multi-randomize
    struct {
      uint8_t num_ccs;
      uint8_t cc_numbers[8];
      uint8_t min_values[8];
      uint8_t max_values[8];
    } multi_random;
  } params;
} action_t;

// Action chain - multiple actions triggered sequentially
#define MAX_ACTIONS_PER_INPUT 4  // Reduced from 8 to save memory

typedef struct {
  uint8_t num_actions;
  action_t actions[MAX_ACTIONS_PER_INPUT];
} action_chain_t;

// Initialize the action system
esp_err_t action_init(void);

// Execute a single action
// trigger_value: for discrete inputs this is boolean (0/1), for continuous it's the value (0-127)
// is_press: true for press/on, false for release/off (only for discrete inputs)
esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press);

// Execute an action chain
esp_err_t action_execute_chain(const action_chain_t* chain, uint8_t trigger_value, bool is_press);

// Helper functions to create common actions
action_t action_create_send_cc(uint8_t cc_number, uint8_t value);
action_t action_create_cc_toggle(uint8_t cc_number, uint8_t value1, uint8_t value2);
action_t action_create_program_next(void);
action_t action_create_program_prev(void);
action_t action_create_scene_next(void);
action_t action_create_scene_prev(void);
action_t action_create_tap_tempo(void);
action_t action_create_transport(action_type_t transport_type);
action_t action_create_all_notes_off(void);
action_t action_create_all_sound_off(void);

// Get action type name (for debugging/console)
const char* action_type_to_string(action_type_t type);

#endif // ACTION_H

