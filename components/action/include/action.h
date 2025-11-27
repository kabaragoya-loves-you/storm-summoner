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
  ACTION_PROGRAM_SET,         // Jump to specific program (0-127)
  ACTION_PROGRAM_BANK_SET,    // Jump to banked program (0-16383, sends bank select + PC)
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
  ACTION_SEND_CC,             // Send CC with value (on press only)
  ACTION_SEND_CC_HOLD,        // Send value1 on press, value2 on release
  ACTION_SEND_CC_CYCLE,       // Cycle through multiple CC values
  ACTION_SEND_DOUBLE_CC,      // 14-bit CC (high resolution)
  ACTION_SEND_NRPN,           // Non-Registered Parameter Number
  ACTION_SEND_RPN,            // Registered Parameter Number
  ACTION_SEND_NOTE_ON,
  ACTION_SEND_NOTE_OFF,
  ACTION_SEND_PC,
  ACTION_SEND_PITCH_BEND,
  ACTION_SEND_AFTERTOUCH,     // Channel aftertouch
  ACTION_SEND_POLY_AFTERTOUCH, // Polyphonic aftertouch
  ACTION_SEND_SONG_SELECT,
  ACTION_SEND_SONG_POSITION,
  ACTION_SEND_MMC,            // MIDI Machine Control
  
  // Randomization
  ACTION_RANDOMIZE_CC,        // Randomize single CC
  ACTION_RANDOMIZE_MULTI,     // Randomize multiple CCs
  
  // MIDI System
  ACTION_SEND_CLOCK_START,    // MIDI Clock Start
  ACTION_SEND_CLOCK_STOP,     // MIDI Clock Stop
  ACTION_SEND_CLOCK_CONTINUE, // MIDI Clock Continue
  ACTION_SEND_RESET,          // System Reset
  ACTION_SEND_TUNE_REQUEST,   // Tune Request
  
  // System
  ACTION_SCREENSAVER_TOGGLE,
  ACTION_CONFIRM_PENDING,     // Confirm pending scene/program change
  ACTION_CANCEL_PENDING,      // Cancel pending change
  ACTION_ALL_NOTES_OFF,       // Send CC123 (All Notes Off)
  ACTION_ALL_SOUND_OFF,       // Send CC120 (All Sound Off)
  
  // Musical concepts (assignable to any input)
  ACTION_SUSTAIN,             // Send CC64 (127 on press, 0 on release)
  ACTION_SOSTENUTO,           // Send CC66 (127 on press, 0 on release)
  
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
    
    // For target actions (program/scene set, 0-127 range)
    struct {
      uint8_t number;
    } target;
    
    // For preset actions (bank+program, 0-16383 range)
    struct {
      uint16_t preset_number;
    } preset;
    
    // For tempo actions
    struct {
      uint8_t bpm_delta;      // For nudge (default 1)
    } tempo;
    
    // For pitch bend
    struct {
      int16_t value;          // -8192 to +8191
    } pitch_bend;
    
    // For aftertouch
    struct {
      uint8_t note;           // For poly aftertouch
      uint8_t pressure;       // 0-127
    } aftertouch;
    
    // For NRPN/RPN
    struct {
      uint16_t parameter;     // 14-bit parameter number
      uint16_t value;         // 14-bit value
    } nrpn;
    
    // For 14-bit CC
    struct {
      uint8_t msb_cc;         // MSB controller number
      uint8_t lsb_cc;         // LSB controller number
      uint16_t value;         // 14-bit value
    } double_cc;
    
    // For song position
    struct {
      uint16_t position;      // 14-bit position
    } song_pos;
    
    // For MMC
    struct {
      uint8_t command;        // MMC command byte
    } mmc;
    
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
action_t action_create_cc_hold(uint8_t cc_number, uint8_t press_value, uint8_t release_value);
action_t action_create_program_next(void);
action_t action_create_program_prev(void);
action_t action_create_scene_next(void);
action_t action_create_scene_prev(void);
action_t action_create_tap_tempo(void);
action_t action_create_transport(action_type_t transport_type);
action_t action_create_all_notes_off(void);
action_t action_create_all_sound_off(void);
action_t action_create_sustain(void);
action_t action_create_sostenuto(void);

// Get action type name (for debugging/console)
const char* action_type_to_string(action_type_t type);

#endif // ACTION_H

