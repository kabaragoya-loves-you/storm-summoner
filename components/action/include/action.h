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
  ACTION_CONTROL_CHANGE,      // Send CC with value (on press only)
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
  ACTION_TOUCHWHEEL_HOLD,     // Set mode on press, restore on release
  ACTION_TOUCHWHEEL_CYCLE,    // Cycle through touchwheel modes
  
  // LFO control
  ACTION_LFO_START,           // Start LFO (slot: 1, 2, or 3=both)
  ACTION_LFO_STOP,            // Stop LFO
  ACTION_LFO_TOGGLE,          // Toggle LFO state
  ACTION_LFO_SHAPE,           // Cycle through waveform shapes
  
  // Clock control (per-scene clock sending)
  ACTION_CLOCK_TOGGLE,        // Toggle clock sending on/off
  ACTION_CLOCK_HOLD,          // Press: one state, Release: opposite state
  ACTION_CLOCK_BURST,         // Hold: send extra clock pulses at multiplier
  
  // MIDI cut control (temporary runtime state)
  ACTION_CUT_TOGGLE,          // Toggle MIDI cut on/off
  ACTION_CUT_HOLD,            // Hold: cut while pressed
  
  // UI module control
  ACTION_SET_UI,              // Switch to a specific UI module
  ACTION_UI_HOLD,             // Switch on press, restore on release
  ACTION_UI_CYCLE,            // Cycle through UI modules on each press
  
  // Touchwheel CC slot 1 control
  ACTION_PARAM_HOLD,          // Hold: swap CC slot 1 on press, restore on release
  ACTION_PARAM_CYCLE,         // Cycle through CC values for slot 1
  
  ACTION_MAX
} action_type_t;

// Action trigger timing (when action takes effect)
typedef enum {
  ACTION_TIMING_IMMEDIATE = 0,   // Execute on trigger (default)
  ACTION_TIMING_NEXT_BEAT,       // Wait for any next beat
  ACTION_TIMING_SPECIFIC_BEAT    // Wait for specific beat (uses timing_beat field)
} action_timing_t;

// Repeat division (matches LFO divisions for consistency)
typedef enum {
  ACTION_REPEAT_16_BARS = 0,  // Every 16 bars
  ACTION_REPEAT_12_BARS,      // Every 12 bars
  ACTION_REPEAT_8_BARS,       // Every 8 bars
  ACTION_REPEAT_4_BARS,       // Every 4 bars
  ACTION_REPEAT_2_BARS,       // Every 2 bars
  ACTION_REPEAT_1_BAR,        // Every bar
  ACTION_REPEAT_HALF,         // Every half note
  ACTION_REPEAT_QUARTER,      // Every quarter note
  ACTION_REPEAT_EIGHTH,       // Every eighth note
  ACTION_REPEAT_SIXTEENTH,    // Every sixteenth note
  ACTION_REPEAT_32ND,         // Every 32nd note
  ACTION_REPEAT_MAX
} action_repeat_division_t;

// ============================================================================
// Morph (interpolation) configuration for CC-based actions
// ============================================================================

// Morph step resolution (how many intermediate values)
typedef enum {
  MORPH_STEPS_AUTO = 0,   // Smart: based on value delta and duration
  MORPH_STEPS_COARSE,     // 8 steps
  MORPH_STEPS_MEDIUM,     // 16 steps
  MORPH_STEPS_FINE,       // 32 steps
  MORPH_STEPS_MANUAL      // User-specified (8-128)
} morph_steps_mode_t;

// Morph timing mode (how duration is determined)
typedef enum {
  MORPH_TIMING_FEEL = 0,  // Curated feel-based timing (fast/medium/slow)
  MORPH_TIMING_DURATION,  // Fixed musical duration (lasts FOR N beats/bars)
  MORPH_TIMING_SYNC       // Sync to moment (ends ON target beat/bar)
} morph_timing_mode_t;

// Curated feel presets (for MORPH_TIMING_FEEL)
typedef enum {
  MORPH_FEEL_FAST = 0,    // ~1/16th note feel
  MORPH_FEEL_MEDIUM,      // ~1 beat feel
  MORPH_FEEL_SLOW         // ~half note feel
} morph_feel_t;

// Musical divisions (for MORPH_TIMING_DURATION and MORPH_TIMING_SYNC)
// Note: Values 0-6 preserved for backward compatibility with saved scenes
typedef enum {
  // Original values (backward compatible)
  MORPH_DIV_1_BEAT = 0,   // 1 beat duration / next beat (was MORPH_DIV_BEAT)
  MORPH_DIV_1_BAR,        // 1 bar duration / next bar (was MORPH_DIV_BAR)
  MORPH_DIV_2_BARS,       // 2 bars
  MORPH_DIV_4_BARS,       // 4 bars
  MORPH_DIV_BEAT_2,       // SYNC: land on beat 2
  MORPH_DIV_BEAT_3,       // SYNC: land on beat 3
  MORPH_DIV_BEAT_4,       // SYNC: land on beat 4
  
  // New duration values (added for DURATION mode)
  MORPH_DIV_2_BEATS,      // 2 beats duration
  MORPH_DIV_3_BEATS,      // 3 beats duration
  MORPH_DIV_3_BARS,       // 3 bars duration
  
  MORPH_DIV_MAX
} morph_division_t;

// Backward compatibility aliases
#define MORPH_DIV_BEAT MORPH_DIV_1_BEAT
#define MORPH_DIV_BAR  MORPH_DIV_1_BAR

// Confirm pending target (Advanced mode only)
typedef enum {
  CONFIRM_TARGET_PRESET = 0,  // Default: confirm preset changes
  CONFIRM_TARGET_SCENE        // Confirm scene changes
} confirm_target_t;

// Action parameters (flexible union for different action types)
typedef struct {
  action_type_t type;
  action_timing_t timing;              // When to execute (default: IMMEDIATE)
  uint8_t timing_beat;                 // Target beat 1-16 (only used when timing == SPECIFIC_BEAT)
  bool repeat_enabled;                 // Whether action repeats at repeat_division
  action_repeat_division_t repeat_division;  // Repeat interval (when repeat_enabled == true)
  uint8_t probability;                 // Chance of firing 10-100% (default 100, only for repeating)
  uint8_t pattern_length;              // Step pattern length 2-8 (0 = disabled, only for repeating)
  uint8_t pattern_mask;                // Bitmask of active steps (bit 0 = step 1)
  
  // Morph configuration (for CONTROL_HOLD, CONTROL_CYCLE, RANDOMIZE)
  bool morph_enabled;                  // Enable smooth value transition
  morph_steps_mode_t morph_steps_mode; // Step resolution
  uint8_t morph_manual_steps;          // 8-128, used when steps_mode=MANUAL
  morph_timing_mode_t morph_timing_mode; // How duration is determined
  morph_feel_t morph_feel;             // Curated feel preset (when timing_mode=FEEL)
  morph_division_t morph_division;     // Musical division (when timing_mode=DURATION or SYNC)
  
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
    
    // For LFO actions (start, stop, toggle, shape)
    struct {
      uint8_t slot;           // 1, 2, or 3 (both)
      uint8_t num_shapes;     // For shape cycle: 2-8 shapes
      uint8_t shapes[8];      // lfo_waveform_t values
      uint8_t current_index;  // Current position in cycle
    } lfo;
    
    // For clock actions (toggle, hold)
    struct {
      bool start_enabled;     // For toggle: what state is "first" (press enables if true)
    } clock;
    
    // For clock burst action
    struct {
      uint16_t speed_percent;  // Speed multiplier: 25, 50, 75, 100, 125, ... 300
    } clock_burst;
    
    // For cut actions (toggle, hold)
    struct {
      uint8_t cut_mode;       // 0=local only, 1=passthrough only, 2=both
    } cut;
    
    // For confirm_pending action (Advanced mode only)
    struct {
      uint8_t target;         // confirm_target_t: 0=preset, 1=scene
    } confirm;
    
    // For UI module actions (set_ui, ui_hold, ui_cycle)
    struct {
      uint8_t module;           // Primary module index (into ui_scene_selectable_modules)
      uint8_t module2;          // For hold: release module index
      uint8_t num_modules;      // For cycle: number of modules (2-8)
      uint8_t modules[8];       // For cycle: module indices
      uint8_t current_index;    // Current position in cycle
    } ui;
    
    // For param slot actions (param_hold, param_cycle)
    struct {
      uint8_t param;            // CC number for press/set
      uint8_t param2;           // For hold: CC number on release
      uint8_t num_params;       // For cycle: number of params (2-8)
      uint8_t params[8];        // For cycle: CC numbers
      uint8_t current_index;    // Current position in cycle
    } tw_param;
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
action_t action_create_touchwheel_hold(uint8_t press_mode, uint8_t release_mode);
action_t action_create_lfo_start(uint8_t slot);
action_t action_create_lfo_stop(uint8_t slot);
action_t action_create_lfo_toggle(uint8_t slot);
action_t action_create_clock_toggle(bool start_enabled);
action_t action_create_clock_hold(bool press_enables);
action_t action_create_clock_burst(uint8_t speed_percent);
action_t action_create_cut_toggle(uint8_t cut_mode);
action_t action_create_cut_hold(uint8_t cut_mode);
action_t action_create_set_ui(uint8_t module_index);
action_t action_create_ui_hold(uint8_t press_module, uint8_t release_module);

// Get action type name (for debugging/console)
const char* action_type_to_string(action_type_t type);

// Check if action type requires hold (press/release) behavior
// These actions should NOT be assigned to bump or on_load
bool action_requires_hold(action_type_t type);

// Action trigger types for validation
typedef enum {
  ACTION_TRIGGER_TOUCHPAD_0_7,   // Touchwheel pads (0-7)
  ACTION_TRIGGER_TOUCHPAD_8_11,  // Discrete pads (Alpha, Bravo, Charlie, Delta)
  ACTION_TRIGGER_BUTTON,         // Left, Right, Both buttons
  ACTION_TRIGGER_BUMP,           // Bump detector (no release event)
  ACTION_TRIGGER_EXPR_SWITCH,    // Expression in switch mode
  ACTION_TRIGGER_ON_LOAD         // Scene load actions
} action_trigger_type_t;

// Check if an action type is valid for a specific trigger
// Returns true if the action can be assigned to the trigger, false otherwise
bool action_is_valid_for_trigger(action_type_t type, action_trigger_type_t trigger);

// Check if action type supports timing options (non-HOLD actions)
// Returns false for HOLD actions that must execute immediately
bool action_supports_timing(action_type_t type);

// Check if action type supports repeat options
// Returns false for preset/scene actions and HOLD actions
bool action_supports_repeat(action_type_t type);

// String conversion for timing (for JSON/display)
// Returns static buffer - copy if needed
const char* action_timing_to_string(action_timing_t timing, uint8_t beat);

// Parse timing from string (e.g., "immediate", "beat", "beat_3")
// Sets timing and beat; defaults to IMMEDIATE if str is NULL or invalid
void action_timing_from_string(const char* str, action_timing_t* timing, uint8_t* beat);

// Validate timing against time signature, remap invalid beats to 1
// Returns true if remapping occurred
bool action_validate_timing(action_t* action, uint8_t beats_per_bar);

// Repeat division string conversion
const char* action_repeat_division_to_string(action_repeat_division_t div);
action_repeat_division_t action_repeat_division_from_string(const char* str);

// Get repeat interval in beats (for scheduling)
// Returns quarter notes equivalent (e.g., 1 bar in 4/4 = 4 beats)
uint8_t action_repeat_division_to_beats(action_repeat_division_t div, uint8_t beats_per_bar);

// Clear all pending actions (call on scene change)
void action_clear_pending(void);

// Stop all repeating instances of a specific action (call on toggle-off or scene change)
void action_stop_repeating(action_t* action);

// ============================================================================
// Morph string conversion functions
// ============================================================================

// Morph steps mode
const char* morph_steps_mode_to_string(morph_steps_mode_t mode);
morph_steps_mode_t morph_steps_mode_from_string(const char* str);

// Morph timing mode
const char* morph_timing_mode_to_string(morph_timing_mode_t mode);
morph_timing_mode_t morph_timing_mode_from_string(const char* str);

// Morph feel
const char* morph_feel_to_string(morph_feel_t feel);
morph_feel_t morph_feel_from_string(const char* str);

// Morph division
const char* morph_division_to_string(morph_division_t div);
morph_division_t morph_division_from_string(const char* str);

// Check if action type supports morphing
bool action_supports_morph(action_type_t type);

// Clear all active morphs (call on scene change)
void action_clear_morphs(void);

// ============================================================================
// CC Value Cache API
// ============================================================================
// Tracks current CC values (0-127) for all 128 CC numbers.
// Used by morphs, param hold/cycle, and continuous mappings.

// Get the cached value for a CC number (returns 0 if not set)
uint8_t action_get_cc_value(uint8_t cc_num);

// Set the cached value for a CC number
void action_set_cc_value(uint8_t cc_num, uint8_t value);

// Reset all CC values to device defaults (call on scene change)
// If device is NULL, resets all to 0
void action_reset_cc_values(const void* device);

#endif // ACTION_H
