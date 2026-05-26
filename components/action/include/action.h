#ifndef ACTION_H
#define ACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Action types - all assignable functions
typedef enum {
  ACTION_NONE = 0,
  
  // Preset (consolidated -- use variant field to pick operation:
  //   VARIANT_SET       = jump to a specific preset/program number
  //   VARIANT_HOLD      = set press preset, restore release preset (or "Original") on release
  //   VARIANT_CYCLE     = step through configured presets on each press
  //   VARIANT_INCREMENT = next preset (was ACTION_PRESET_INC / program_next)
  //   VARIANT_DECREMENT = previous preset (was ACTION_PRESET_DEC / program_prev)
  ACTION_PRESET,
  // Scene (consolidated -- use variant field to pick operation:
  //   VARIANT_SET       = jump to a specific scene number (was ACTION_SCENE)
  //   VARIANT_INCREMENT = next scene (was ACTION_SCENE_INC)
  //   VARIANT_DECREMENT = previous scene (was ACTION_SCENE_DEC))
  ACTION_SCENE,
  
  // Transport (consolidated -- use variant field to pick operation:
  //   VARIANT_PLAY   = toggle play/stop (was ACTION_PLAY)
  //   VARIANT_STOP   = stop transport (was ACTION_STOP)
  //   VARIANT_PAUSE  = pause only, does not unpause (was ACTION_PAUSE)
  //   VARIANT_RECORD = toggle record/stop (was ACTION_RECORD))
  ACTION_TRANSPORT,
  
  // Tempo (consolidated -- use variant field to pick operation:
  //   VARIANT_TAP, VARIANT_SET, VARIANT_INCREMENT, VARIANT_DECREMENT,
  //   VARIANT_HOLD, VARIANT_CYCLE)
  ACTION_TEMPO,
  
  // Direct MIDI output
  // Control (consolidated -- use variant field to pick operation:
  //   VARIANT_SET    = send CC with value(s) on press (legacy "Control Change")
  //   VARIANT_HOLD   = send press values, then release values (with release_mode)
  //   VARIANT_CYCLE  = cycle through multiple CC values per press)
  ACTION_CONTROL,
  ACTION_NOTE,                // Send Note On on press, Note Off on release
  
  // Randomization
  ACTION_RANDOMIZE,           // Randomize one or more CCs (uses multi_random params)
  
  // System
  ACTION_CONFIRM_PENDING,     // Confirm pending scene/program change
  ACTION_RESET,               // Send CC123 + CC120 + System Reset (0xFF)
  
  // Piano pedals (single hold action; cc_number param picks which switch-style
  // MIDI CC fires on press/release. Supported CCs:
  //   64 = Damper (Sustain),  66 = Sostenuto,  67 = Soft (Una Corda),
  //   68 = Legato Footswitch, 69 = Hold 2.
  // CC 65 (Portamento on/off) is intentionally excluded -- it's a synth
  // engine flag rather than a physical-pedal concept.
  ACTION_PIANO_PEDAL,
  
  // Touchwheel mode control (consolidated -- variants HOLD / CYCLE)
  //   HOLD  = set mode on press, restore on release (or capture/restore the
  //           live mode when release_to_original is set)
  //   CYCLE = step through a list of 2-8 modes, one per press
  ACTION_TOUCHWHEEL,
  
  // LFO control (consolidated -- variants START / STOP / TOGGLE / MODIFY)
  //   START  / STOP / TOGGLE = drive the LFO engine for slot 1, 2, or 3 (both)
  //   MODIFY = apply per-parameter overrides to a running LFO (waveform,
  //            rate mode, rate, polarity, floor, ceiling, resolution, steps).
  //            Each override has an "Original" sentinel meaning "do not
  //            touch this field". Replaces the old SHAPE-cycle behavior.
  ACTION_LFO,
  
  // Clock control (consolidated -- variants TOGGLE / HOLD / BURST)
  //   TOGGLE = flip scene send_clock on each press (was ACTION_CLOCK_TOGGLE)
  //   HOLD   = press one state, release opposite (was ACTION_CLOCK_HOLD)
  //   BURST  = extra clock pulses while held (was ACTION_CLOCK_BURST)
  ACTION_CLOCK,
  
  // MIDI cut control (temporary runtime state)
  ACTION_CUT,                 // Toggle or Hold via variant
  
  // UI module control
  ACTION_SET_UI,              // Switch to a specific UI module
  ACTION_UI_HOLD,             // Switch on press, restore on release
  ACTION_UI_CYCLE,            // Cycle through UI modules on each press
  
  // Touchwheel CC slot 1 control
  ACTION_PARAM_HOLD,          // Hold: swap CC slot 1 on press, restore on release
  ACTION_PARAM_CYCLE,         // Cycle through CC values for slot 1

  // RTG control
  ACTION_RTG_TOGGLE,          // Toggle RTG enabled state
  ACTION_RTG_HOLD,            // Press: enable RTG, Release: disable RTG

  // Sample+Hold control
  ACTION_SAMPLE_HOLD_TOGGLE,  // Toggle S+H enabled state
  ACTION_SAMPLE_HOLD_HOLD,    // Press: enable S+H, Release: disable S+H

  // RTG/S+H step control
  ACTION_STEP,                // Trigger RTG or S+H step

  // Looper punch-in
  ACTION_PUNCH_IN,            // Send start CC at bar, finish CC after duration

  // Flag ceremony (scene-local semaphore)
  ACTION_FLAG_CEREMONY,       // Check flag state, send CC, flip flag

  // Boomerang (ADSR-style envelope: dive to target, hold, return)
  ACTION_BOOMERANG,           // 3-phase envelope on any continuous output
  
  ACTION_MAX
} action_type_t;

// Action variant -- secondary discriminator for consolidated action families
// (e.g. ACTION_TEMPO uses variant to distinguish Tap / Set / Inc / Dec / Hold
// / Cycle). Singleton actions (NOTE, BOOMERANG, etc.) use VARIANT_NONE.
// One shared enum across families: each family uses the subset that makes
// sense for it. Keep additions append-only so JSON ordinals stay stable.
typedef enum {
  VARIANT_NONE = 0,
  VARIANT_INCREMENT,
  VARIANT_DECREMENT,
  VARIANT_SET,
  VARIANT_HOLD,
  VARIANT_CYCLE,
  VARIANT_TOGGLE,
  VARIANT_START,
  VARIANT_STOP,
  VARIANT_TAP,
  VARIANT_BURST,
  // Transport family operations (append-only after BURST -- keeps JSON
  // ordinals stable for everything before this point).
  VARIANT_PLAY,
  VARIANT_PAUSE,
  VARIANT_RECORD,
  // LFO family parameter-override variant (replaces the old SHAPE-cycle
  // action). Append-only -- do not move above this point.
  VARIANT_MODIFY,
  VARIANT_MAX
} action_variant_t;

// ============================================================================
// Boomerang (ADSR envelope action) types
// ============================================================================

// Per-phase duration mode (attack, sustain, release)
typedef enum {
  BOOMERANG_DUR_INSTANT = 0,   // Zero-time transition (jump)
  BOOMERANG_DUR_TIME_MS,       // Fixed millisecond duration
  BOOMERANG_DUR_DIVISION       // Tempo-synced musical division
} boomerang_duration_mode_t;

// Target value mode
typedef enum {
  BOOMERANG_TARGET_EXPLICIT = 0, // Use configured target_value
  BOOMERANG_TARGET_RANDOM        // Pick random value in valid range at trigger time
} boomerang_target_mode_t;

// Starting value mode: where the envelope begins (and returns to on release)
typedef enum {
  BOOMERANG_START_CURRENT = 0,   // Capture the live parameter value at trigger time
  BOOMERANG_START_EXPLICIT       // Use configured start_value
} boomerang_start_mode_t;

// Step action target
typedef enum {
  STEP_TARGET_SH = 0,         // Sample & Hold (future)
  STEP_TARGET_RTG             // Random Tone Generator
} step_target_t;

// Punch-in duration (for looper recording)
typedef enum {
  PUNCH_IN_1_BEAT = 0,
  PUNCH_IN_2_BEATS,
  PUNCH_IN_3_BEATS,
  PUNCH_IN_4_BEATS,
  PUNCH_IN_5_BEATS,
  PUNCH_IN_6_BEATS,
  PUNCH_IN_7_BEATS,           // Covers up to 8/4 time
  PUNCH_IN_1_BAR,
  PUNCH_IN_2_BARS,
  PUNCH_IN_4_BARS,
  PUNCH_IN_8_BARS,
  PUNCH_IN_16_BARS
} punch_in_duration_t;

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
  action_variant_t variant;            // Secondary operation discriminator (see action_variant_t)
  action_timing_t timing;              // When to execute (default: IMMEDIATE)
  uint8_t timing_beat;                 // Target beat 1-16 (only used when timing == SPECIFIC_BEAT)
  bool repeat_enabled;                 // Whether action repeats at repeat_division
  action_repeat_division_t repeat_division;  // Repeat interval (when repeat_enabled == true)
  uint8_t probability;                 // Chance of firing 10-100% (default 100, only for repeating)
  uint8_t pattern_length;              // Step pattern length 2-8 (0 = disabled, only for repeating)
  uint8_t pattern_mask;                // Bitmask of active steps (bit 0 = step 1)
  bool transport_trigger;              // Auto-trigger when transport starts (for repeating actions)
  bool raise_flag;                     // Set scene flag to 1 after action completes (when flag system enabled)
  
  // Morph configuration (for ACTION_CONTROL+VARIANT_HOLD/CYCLE, RANDOMIZE)
  bool morph_enabled;                  // Enable smooth value transition
  morph_steps_mode_t morph_steps_mode; // Step resolution
  uint8_t morph_manual_steps;          // 8-128, used when steps_mode=MANUAL
  morph_timing_mode_t morph_timing_mode; // How duration is determined
  morph_feel_t morph_feel;             // Curated feel preset (when timing_mode=FEEL)
  morph_division_t morph_division;     // Musical division (when timing_mode=DURATION or SYNC)

  // Follow-Up configuration. Hold variants honor this to optionally skip
  // their release-phase work based on how long the trigger was held. Only
  // actions for which action_supports_followup_for() returns true look at
  // these fields; everything else ignores them and behaves as before.
  //   followup_mode == 0 (Always)  -> always fire release (default; no-op)
  //   followup_mode == 1 (If Held) -> fire release only if elapsed >= threshold
  //   followup_mode == 2 (If Quick)-> fire release only if elapsed <  threshold
  uint8_t followup_mode;
  uint16_t followup_threshold_ms;      // 500/750/1000/1500/2000; 0 -> default 1000
  int64_t hold_press_time_us;          // Transient: stamped on press for elapsed calc

  union {
    // For ACTION_CONTROL (variants SET / HOLD / CYCLE) - supports 1-4 CC numbers
    struct {
      uint8_t num_ccs;            // 1-4 CC numbers (1 = single CC mode)
      uint8_t cc_numbers[4];      // The CC numbers
      uint8_t values[4];          // Primary values for CONTROL (one per CC)
      uint8_t values2[4];         // Release values for VARIANT_HOLD (one per CC)
      uint8_t num_cycle_steps;    // For cycle: 2-8 steps (shared across all CCs)
      uint8_t cycle_values[4][8]; // For cycle: [cc_idx][step]
      uint8_t current_index;      // Current position in cycle (shared)
    } control;
    
    // For note actions (hold-style: press=on, release=off)
    struct {
      uint8_t note;
      uint8_t velocity;
    } note;

    // For ACTION_PIANO_PEDAL: toggles a switch-style MIDI CC (127 on press,
    // 0 on release). cc_number is one of the standard piano pedal CCs:
    //   64=Damper, 66=Sostenuto, 67=Soft, 68=Legato, 69=Hold 2.
    struct {
      uint8_t cc_number;
    } piano_pedal;
    
    // For scene set (0-127 range)
    struct {
      uint8_t number;
    } target;
    
    // For ACTION_PRESET (consolidated -- variants SET / HOLD / CYCLE /
    // INCREMENT / DECREMENT). Bank-aware values use the full uint16_t range
    // (0-16383 with bank mode, 0-127 without).
    struct {
      uint16_t program;            // VARIANT_SET: target preset
      uint16_t press_preset;       // VARIANT_HOLD: preset to set on press
      uint16_t release_preset;     // VARIANT_HOLD: preset to set on release (used when release_to_original == 0)
      uint8_t  release_to_original;// VARIANT_HOLD: 0 = use release_preset (default), 1 = restore captured preset
      uint16_t captured_preset;    // VARIANT_HOLD transient: device preset snapshot taken at press time; not persisted
      uint8_t  num_presets;        // VARIANT_CYCLE: 2-8 steps
      uint16_t cycle_presets[8];   // VARIANT_CYCLE: preset values
      uint8_t  current_index;      // VARIANT_CYCLE: rotating cursor (mutable at runtime)
    } preset;
    
    // For tempo actions (set, hold, cycle, increment, decrement)
    struct {
      uint16_t bpm;               // VARIANT_SET: 20-300, 0=random, 0xFFFF=original
      uint16_t random_floor;      // VARIANT_SET + random: lower bound (default 20)
      uint16_t random_ceiling;    // VARIANT_SET + random: upper bound (default 300)
      uint16_t press_bpm;         // For VARIANT_HOLD: tempo on press
      uint16_t release_bpm;       // For VARIANT_HOLD: tempo on release
      uint8_t num_tempos;         // For VARIANT_CYCLE: number of tempos (2-8)
      uint16_t cycle_tempos[8];   // For VARIANT_CYCLE: tempo values
      uint8_t current_index;      // For VARIANT_CYCLE: current position
      uint8_t inc_amount;         // For VARIANT_INCREMENT/DECREMENT: step
                                  // (1,2,3,4,5,10,15,20). Treated as 1 if
                                  // 0 so legacy scenes Just Work.
    } tempo;
    
    // For randomize (one or more CCs, always 0-127 range)
    struct {
      uint8_t num_ccs;
      uint8_t cc_numbers[8];
    } randomize;
    
    // For ACTION_TOUCHWHEEL (consolidated -- variants HOLD / CYCLE).
    // Storage is user-facing mode indices (0 .. NUM_TOUCHWHEEL_USER_MODES-1),
    // NOT touchwheel_mode_t engine enums. The handler resolves the user
    // index through apply_touchwheel_mode_runtime() at dispatch time.
    struct {
      uint8_t mode;                // VARIANT_HOLD: press mode (user index)
      uint8_t mode2;               // VARIANT_HOLD: release mode (user index; used when release_to_original == 0)
      uint8_t release_to_original; // VARIANT_HOLD: 0 = restore mode2 (default), 1 = restore captured_mode
      uint8_t captured_mode;       // VARIANT_HOLD transient: live mode snapshot taken at press time; not persisted
      uint8_t num_modes;           // VARIANT_CYCLE: number of modes (2-8)
      uint8_t modes[8];            // VARIANT_CYCLE: user-mode indices per step
      uint8_t current_index;       // VARIANT_CYCLE: rotating cursor (mutable at runtime)
    } tw_mode;
    
    // For ACTION_LFO (consolidated -- variants START / STOP / TOGGLE / MODIFY).
    // START/STOP/TOGGLE only read `slot`. MODIFY adds per-parameter override
    // slots; each field's "Original" sentinel (documented per-field) means
    // "leave the scene config's value in place". This replaces the old
    // SHAPE cycling fields (num_shapes/shapes[]/current_index were removed
    // when cycling was dropped from the LFO family).
    struct {
      uint8_t slot;              // All variants: 1, 2, or 3 (both)
      // MODIFY overrides:
      uint8_t waveform;          // lfo_waveform_t,        0xFF = Original, 0xFE = Random
      uint8_t rate_mode;         // lfo_rate_mode_t,       0xFF = Original, 0xFE = Random
      uint16_t rate_hz_x100;     // Free-rate Hz * 100,  0xFFFF = Original, 0xFFFE = Random
      uint8_t division;          // lfo_note_division_t,   0xFF = Original, 0xFE = Random
      uint8_t polarity;          // polarity_t,            0xFF = Original, 0xFE = Random
      uint8_t floor;             // 0-127,                 0xFF = Original, 0xFE = Random
      uint8_t ceiling;           // 0-127,                 0xFF = Original, 0xFE = Random
      uint8_t resolution_mode;   // lfo_resolution_mode_t, 0xFF = Original, 0xFE = Random
      uint8_t manual_steps;      // 1-N (Manual mode only),   0 = Original, 254 = Random
    } lfo;
    
    // For ACTION_CLOCK (consolidated -- variants TOGGLE / HOLD / BURST).
    struct {
      bool start_enabled;      // TOGGLE + HOLD: press/enable polarity
      uint16_t speed_percent;  // BURST: 25-300 in 25% steps (default 100)
    } clock;
    
    // For ACTION_CUT (Toggle/Hold variants)
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

    // For step action (RTG/S+H trigger)
    struct {
      uint8_t target;           // step_target_t: 0=S+H, 1=RTG
    } step;

    // For punch-in action (looper recording)
    struct {
      uint8_t start_cc;         // CC number to send at start
      uint8_t start_value;      // CC value to send at start
      uint8_t finish_cc;        // CC number to send at end
      uint8_t finish_value;     // CC value to send at end
      punch_in_duration_t duration;  // How long to record
    } punch_in;

    // For flag ceremony action (scene-local semaphore)
    struct {
      uint8_t flag_up_cc;       // CC number to send when flag is up (1)
      uint8_t flag_up_value;    // CC value to send when flag is up
      uint8_t flag_down_cc;     // CC number to send when flag is down (0)
      uint8_t flag_down_value;  // CC value to send when flag is down
    } flag_ceremony;

    // For boomerang action (ADSR envelope on any continuous output)
    struct {
      uint8_t output_type;          // output_type_t (CC, TEMPO, LFO_RATE, etc.)
      uint8_t lfo_target;           // lfo_target_t (for LFO_RATE/LFO_DEPTH outputs)
      uint8_t cc_number;            // CC number (when output_type == CC)
      uint8_t target_mode;          // boomerang_target_mode_t: EXPLICIT or RANDOM
      uint16_t target_value;        // Target value (range depends on output_type)

      uint8_t start_mode;           // boomerang_start_mode_t: CURRENT or EXPLICIT
      uint16_t start_value;         // Override start value when start_mode == EXPLICIT

      // Attack phase (travel out to target)
      uint8_t attack_mode;          // boomerang_duration_mode_t
      uint16_t attack_time_ms;      // Duration when attack_mode == TIME_MS
      uint8_t attack_division;      // morph_division_t when attack_mode == DIVISION
      uint8_t attack_curve;         // curve_type_t
      uint8_t attack_curve_slope;   // curve_slope_t

      // Sustain phase (hold at target)
      uint8_t sustain_mode;         // boomerang_duration_mode_t
      uint16_t sustain_time_ms;
      uint8_t sustain_division;

      // Release phase (return to captured start)
      uint8_t release_mode;         // boomerang_duration_mode_t
      uint16_t release_time_ms;
      uint8_t release_division;
      uint8_t release_curve;        // curve_type_t
      uint8_t release_curve_slope;  // curve_slope_t
    } boomerang;
  } params;
} action_t;

// Maximum on_load actions per scene
#define MAX_ON_LOAD_ACTIONS 4

// Maximum on_play actions per scene
#define MAX_ON_PLAY_ACTIONS 4

// "Original" sentinel values for ACTION_LFO + VARIANT_MODIFY override
// fields. Each field carries one of these in its default state, meaning
// "do not touch this field at dispatch time; the scene's configured value
// stays in effect". Picked to fall outside each field's valid range:
//   - 8-bit enums / 0-127 ranges:    0xFF
//   - rate_hz_x100 (uint16_t):     0xFFFF
//   - manual_steps (valid 1-N):    0
// Random sentinels (second picker entry on MODIFY rollers) pick a fresh
// value from the field's valid range on every trigger:
//   - 8-bit enums / 0-127 ranges:    0xFE
//   - rate_hz_x100 (uint16_t):     0xFFFE
//   - manual_steps:                  254 (0 is Original; 255 is a valid step count)
#define ACTION_LFO_ORIG_U8      ((uint8_t)0xFF)
#define ACTION_LFO_ORIG_U16     ((uint16_t)0xFFFF)
#define ACTION_LFO_ORIG_STEPS   ((uint8_t)0)
#define ACTION_LFO_RAND_U8      ((uint8_t)0xFE)
#define ACTION_LFO_RAND_U16     ((uint16_t)0xFFFE)
#define ACTION_LFO_RAND_STEPS   ((uint8_t)254)

// VARIANT_SET tempo sentinels (outside the 20-300 BPM picker range).
#define ACTION_TEMPO_BPM_RANDOM   ((uint16_t)0)
#define ACTION_TEMPO_BPM_ORIGINAL ((uint16_t)0xFFFF)

// Initialize the action system
esp_err_t action_init(void);

// Execute a single action
// trigger_value: for discrete inputs this is boolean (0/1), for continuous it's the value (0-127)
// is_press: true for press/on, false for release/off (only for discrete inputs)
esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press);

// Helper functions to create common actions
// Only the helpers actually used by scene defaults and the console are kept;
// other action types are built directly via struct initialization (JSON loader,
// menu UI) which keeps the surface area small.
action_t action_create_control(uint8_t cc_number, uint8_t value);
action_t action_create_preset_inc(void);
action_t action_create_preset_dec(void);
action_t action_create_scene_inc(void);
action_t action_create_scene_dec(void);
action_t action_create_tap_tempo(void);
action_t action_create_set_tempo(uint16_t bpm);
action_t action_create_transport(action_variant_t variant);
action_t action_create_reset(void);
action_t action_create_piano_pedal(uint8_t cc_number);

// Get action type name (for debugging/console and the type-picker roller).
// Returns the BASE family name only for consolidated types
// (e.g. ACTION_TEMPO -> "Tempo"). For everywhere the user sees an
// already-configured action use action_get_display_name() instead.
const char* action_type_to_string(action_type_t type);

// Get human-facing name for an action variant ("Hold", "Cycle", "Increment").
// Used by the variant-picker roller. Returns empty string for VARIANT_NONE
// or out-of-range values.
const char* action_variant_to_string(action_variant_t variant);

// True if the given action type is a consolidated family that exposes
// variants in the UI (e.g. ACTION_TEMPO). Singletons return false.
#include <stddef.h>
bool action_type_has_variants(action_type_t type);

// Write the user-facing display name for an action into buf. For
// consolidated families, returns a compact per-variant label tuned for the
// device's narrow ~12-14 char display (e.g. "Tempo Hold", "Tempo +1",
// "Set Tempo", "Tap Tempo"). For singletons or unknown variants, falls
// back to the type name. Always null-terminates.
void action_get_display_name(const action_t* action, char* buf, size_t len);

// Check if action type requires hold (press/release) behavior.
// Returns true only when EVERY variant of the type is hold-like.
// For consolidated families (e.g. ACTION_TEMPO) prefer the variant-aware
// action_requires_hold_for() at runtime; the by-type form returns the
// conservative answer used by menu filters.
bool action_requires_hold(action_type_t type);

// Variant-aware hold check. Use this anywhere the full action_t is
// available (runtime dispatch, scheduler, etc.) so consolidated families
// (e.g. ACTION_TEMPO with VARIANT_HOLD) are correctly treated as hold.
bool action_requires_hold_for(const action_t* action);

// True for hold actions where it makes sense to optionally skip the
// release-phase work based on hold duration ("Follow-Up"). Excludes
// NOTE / SUSTAIN / SOSTENUTO (stuck-note/pedal risk) and non-paired
// types like LFO_TOGGLE / LFO_SHAPE / CLOCK_BURST.
bool action_supports_followup_for(const action_t* action);

// Action trigger types for validation
typedef enum {
  ACTION_TRIGGER_TOUCHPAD_0_7,   // Touchwheel pads (0-7)
  ACTION_TRIGGER_TOUCHPAD_8_11,  // Discrete pads (Alpha, Bravo, Charlie, Delta)
  ACTION_TRIGGER_BUTTON,         // Left, Right, Both buttons
  ACTION_TRIGGER_BUMP,           // Bump detector (no release event)
  ACTION_TRIGGER_EXPR_SWITCH,    // Expression in switch mode
  ACTION_TRIGGER_ON_LOAD,        // Scene load actions
  ACTION_TRIGGER_ON_PLAY         // Transport play actions (fresh start only)
} action_trigger_type_t;

// Flattened trigger capabilities. Conservative answer per field:
// "true means the trigger supports / participates in this thing".
// One source of truth for what each trigger does; the validator below
// uses it to decide which actions/variants are valid where.
typedef struct {
  bool delivers_release;    // false for BUMP, ON_LOAD, ON_PLAY (no release pair)
  bool inhibits_transport;  // true for ON_PLAY (firing transport here would recurse)
  bool fires_at_load_time;  // true for ON_LOAD (scene not fully live yet)
  bool fires_at_play_time;  // true for ON_PLAY (transport-start moment)
} trigger_capabilities_t;

trigger_capabilities_t action_trigger_capabilities(action_trigger_type_t trigger);

// Check if an action type is valid for a specific trigger.
// Thin wrapper around action_is_valid_for_trigger_for() that synthesizes a
// default-variant action_t. Prefer the _for() form when you have a real
// action_t (variant-precise; closes the TEMPO/CONTROL+HOLD gap).
bool action_is_valid_for_trigger(action_type_t type, action_trigger_type_t trigger);

// Canonical entry point. Variant-aware. All call sites that have a full
// action_t should use this -- it correctly rejects consolidated-family
// HOLD variants on bump/on_load/on_play, where action_requires_hold_for()
// alone would not (HOLD types are removed from the by-type hold_actions[]
// table for consolidated families).
bool action_is_valid_for_trigger_for(const action_t* action,
                                     action_trigger_type_t trigger);

// Variant picker filter -- "would picking THIS variant on the current
// trigger be valid?". Uses action_is_valid_for_trigger_for() on a
// synthetic action_t. Drives the Variant rollers in action_config.c.
bool action_variant_is_valid_for_trigger(action_type_t type,
                                         action_variant_t variant,
                                         action_trigger_type_t trigger);

// Fire-and-forget category: action that sends a thing once and returns
// without needing a release pair, mode interaction, or live scene state.
// Replaces the old is_on_load_allowed / is_on_play_allowed whitelists.
// Variant-aware: ACTION_CONTROL+SET is fire-and-forget, +HOLD/CYCLE are not.
bool action_is_fire_and_forget_for(const action_t* action);

// Transport actions (PLAY/STOP/PAUSE/RECORD). Helper used by the validator
// to enforce the on_play recursion guard.
bool action_is_transport(action_type_t type);

// Check if action type supports timing options (non-HOLD actions)
// Returns false for HOLD actions that must execute immediately.
// Conservative answer at type level: consolidated families return true if
// ANY variant supports timing. Use action_supports_timing_for() at runtime
// for the variant-precise answer.
bool action_supports_timing(action_type_t type);

// Check if action type supports repeat options
// Returns false for preset/scene actions and HOLD actions.
// Conservative answer at type level: consolidated families return false if
// NO variant supports repeat. Use action_supports_repeat_for() at runtime
// for the variant-precise answer.
bool action_supports_repeat(action_type_t type);

// Variant-aware timing/repeat predicates. Prefer these when the caller has
// the full action_t. For ACTION_TEMPO:
//   - timing: false for VARIANT_TAP (no meaning to defer) and VARIANT_HOLD
//   - repeat: true for VARIANT_INCREMENT, VARIANT_DECREMENT, VARIANT_CYCLE
//             false for VARIANT_TAP, VARIANT_SET, VARIANT_HOLD
// For all other types these return the same answer as the by-type forms.
bool action_supports_timing_for(const action_t* action);
bool action_supports_repeat_for(const action_t* action);

// Check if action supports transport trigger (auto-start when transport plays)
// Only valid for actions that support timing and repeat
bool action_supports_transport_trigger(action_type_t type);

// String conversion for timing (for JSON/display)
// Returns static buffer - copy if needed
const char* action_timing_to_string(action_timing_t timing, uint8_t beat);

// Parse timing from string (e.g., "immediate", "beat", "beat_3")
// Sets timing and beat; defaults to IMMEDIATE if str is NULL or invalid
void action_timing_from_string(const char* str, action_timing_t* timing, uint8_t* beat);

// Validate timing against time signature, remap invalid beats to 1
// Returns true if remapping occurred
bool action_validate_timing(action_t* action, uint8_t beats_per_bar);

// Validate every action timing in a scene against its time signature.
// Call after scene load and when time signature changes.
// Forward-declared to avoid pulling scene.h here (scene.h includes action.h).
struct scene_t;
void action_validate_scene_timings(struct scene_t* scene);

// Repeat division string conversion
const char* action_repeat_division_to_string(action_repeat_division_t div);
action_repeat_division_t action_repeat_division_from_string(const char* str);

// Get repeat interval in beats (for scheduling)
// Returns quarter notes equivalent (e.g., 1 bar in 4/4 = 4 beats)
uint8_t action_repeat_division_to_beats(action_repeat_division_t div, uint8_t beats_per_bar);

// Punch-in duration string conversion
const char* punch_in_duration_to_string(punch_in_duration_t duration);
const char* punch_in_duration_to_display_string(punch_in_duration_t duration);
punch_in_duration_t punch_in_duration_from_string(const char* str);

// Get punch-in duration in beats (for scheduling finish CC)
uint8_t punch_in_duration_to_beats(punch_in_duration_t duration, uint8_t beats_per_bar);

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

// Check if action type supports morphing.
// Conservative answer at type level: consolidated families return true if
// ANY variant supports morph. Use action_supports_morph_for() when you have
// the full action_t for the variant-precise answer (e.g. ACTION_CONTROL
// supports morph for HOLD/CYCLE but not SET).
bool action_supports_morph(action_type_t type);
bool action_supports_morph_for(const action_t* action);

// Clear all active morphs (call on scene change)
void action_clear_morphs(void);

// ============================================================================
// Boomerang Engine API
// ============================================================================

// Clear all active boomerang envelopes (call on scene change)
void action_clear_boomerangs(void);

// Last pitch-bend value sent (center = 8192). Updated whenever the boomerang
// engine or other pitch-bend output path sends a pitch-bend message.
int16_t action_get_last_pitch_bend(void);
void action_set_last_pitch_bend(int16_t value);

// ============================================================================
// Flag System API (scene-local semaphore)
// ============================================================================

// Check if action type supports the "Raise the Flag" option (by-type).
// Conservative answer for consolidated families: returns true if ANY variant
// supports it. Callers with a full action_t should prefer the variant-aware
// action_supports_raise_flag_for() below, which excludes HOLD variants per
// the rationale that holds change state only for the duration of the press.
bool action_supports_raise_flag(action_type_t type);

// Variant-aware companion. Returns false for any hold-shaped action --
// HOLD variants of consolidated families (TEMPO, CONTROL, PRESET), explicit
// *_HOLD singletons, and press/release pairings like ACTION_NOTE -- because
// the release event would immediately unflag whatever the press flagged,
// defeating the flag-as-semaphore use case.
bool action_supports_raise_flag_for(const action_t* action);

// Clear the scene flag (call on scene change)
void action_clear_flag(void);

// Get current flag state (0 or 1)
uint8_t action_get_flag(void);

// Set flag state
void action_set_flag(uint8_t value);

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
