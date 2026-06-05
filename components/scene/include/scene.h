#ifndef SCENE_H
#define SCENE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "midi_messages.h"
#include "action.h"
#include "continuous_mapping.h"
#include "expression.h"
#include "input_mode.h"
#include "tempo.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "assets_types.h"

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
  TOUCHWHEEL_MODE_PADS,           // Each pad acts as individual pad
  TOUCHWHEEL_MODE_PROGRAM_CHANGE, // Endless encoder to dial program numbers
  TOUCHWHEEL_MODE_CONTINUOUS,     // Continuous CC data source (0-127)
  TOUCHWHEEL_MODE_SET_TEMPO,      // Set BPM (20-300), default endless
  TOUCHWHEEL_MODE_PITCH_BEND,     // Pitch bend (-8192 to 8191), bipolar only
  TOUCHWHEEL_MODE_AFTERTOUCH,     // Channel aftertouch (0-127), default odometer
  TOUCHWHEEL_MODE_DOUBLE_CC,      // Double CC (0-16383), default odometer
  TOUCHWHEEL_MODE_VELOCITY,       // Velocity source for note-generating modules (internal only)
  TOUCHWHEEL_MODE_LFO_RATE,       // LFO rate modulation source (internal only)
  TOUCHWHEEL_MODE_LFO_DEPTH,      // LFO depth modulation source (internal only)
  TOUCHWHEEL_MODE_RTG_RATE        // RTG rate modulation source (internal only)
} touchwheel_mode_t;

// Touchwheel continuous style (for modes that use continuous input)
typedef enum {
  TOUCHWHEEL_STYLE_ODOMETER,      // Position-based 0-100% (~15 discrete values)
  TOUCHWHEEL_STYLE_ENDLESS,       // Incremental encoder (full range)
  TOUCHWHEEL_STYLE_BIPOLAR        // Bipolar center-return for pitch bend
} touchwheel_style_t;

// Touchpad mapping
typedef struct {
  bool enabled;               // Whether this touchpad is active
  action_t action;            // Action to execute
} touchpad_mapping_t;

// Maximum length of a UI module name (must fit scene_t.ui_module field)
#define MAX_UI_MODULE_NAME 16

#define NUM_CC_TRIGGERS 4

typedef struct {
  uint8_t cc_number;
  action_t action;
  bool pressing;
} cc_trigger_slot_t;

// Scene structure
typedef struct scene_t {
  char name[17];              // Scene name (max 16 chars + null)
  char device_id[64];         // Device slug (empty = use global device_config)
  uint8_t midi_channel;       // Per-scene MIDI channel (0 = use global, 1-16 = override)
  uint8_t note_channel;       // Note output channel override (0 = use scene channel, 1-16 = specific)
  uint8_t trs_type;           // Per-scene TRS polarity (0 = use global, 1=A, 2=B, 3=TS, 4=Both)
  char ui_module[MAX_UI_MODULE_NAME]; // UI module to load with scene (empty = "beat")
  
  // Program change settings (modes 2 & 3)
  uint8_t program_number;     // PC value (0-127)
  bool send_pc_on_load;       // Send PC when loading this scene
  
  // Scene load actions (up to 4 actions executed when scene loads)
  uint8_t num_on_load_actions;
  action_t on_load[MAX_ON_LOAD_ACTIONS];
  
  // Transport play actions (up to 4 actions executed when transport starts playing)
  uint8_t num_on_play_actions;
  action_t on_play[MAX_ON_PLAY_ACTIONS];
  
  // Touchwheel configuration
  touchwheel_mode_t touchwheel_mode;
  touchwheel_style_t touchwheel_style;  // For CONTINUOUS mode: odometer vs endless encoder
  continuous_mapping_t touchwheel;      // For TOUCHWHEEL_MODE_CONTINUOUS (like proximity/cv/etc)
  lfo_target_t touchwheel_lfo_target;   // Which LFO(s) to affect in LFO_RATE/LFO_DEPTH modes
  uint8_t touchwheel_initial_value;     // Initial CC value for endless encoder (0-127, default 0)
  
  // Discrete input assignments (single action per input)
  touchpad_mapping_t touchpads[NUM_TOUCHPADS];
  action_t button_left;
  action_t button_right;
  action_t button_both;
  action_t bump;               // Bump detector (one-shot trigger)
  
  // Continuous input mappings
  continuous_mapping_t expression;
  continuous_mapping_t cv;
  continuous_mapping_t proximity;
  continuous_mapping_t als;          // Ambient light sensor
  continuous_mapping_t tilt_x;       // Accelerometer X axis (roll, left/right)
  continuous_mapping_t tilt_y;       // Accelerometer Y axis (pitch, forward/back)
  continuous_mapping_t note_track;   // Incoming MIDI Note On as a continuous source
  cc_trigger_slot_t cc_triggers[NUM_CC_TRIGGERS];

  // Expression jack configuration (shared jack, multiple modes)
  expression_mode_t expression_mode; // PEDAL, SUSTAIN, SOSTENUTO, GATE, SWITCH
  action_t sustain;            // Action for sustain pedal events
  action_t sostenuto;          // Action for sostenuto pedal events
  action_t expr_switch;        // Action for switch mode (flexible action trigger)
  
  // CV input configuration
  input_mode_t cv_input_mode;        // CV, CLOCK_SYNC, AUDIO, or NOTE
  
  // CV NOTE mode velocity configuration (when cv_input_mode = NOTE)
  velocity_mode_t cv_velocity_mode;      // FIXED, GATE_VOLTAGE, or TOUCHWHEEL
  uint8_t cv_velocity;                   // Fixed velocity value (1-127)
  
  // Audio envelope follower configuration (when cv_input_mode = AUDIO)
  audio_config_t audio_config;

  // CV Trigger mode configuration (when cv_input_mode = TRIGGER)
  action_t cv_trigger_action;
  uint8_t cv_trigger_threshold;      // 0-100 percent of 0-3.3V range (default 50)
  uint16_t cv_trigger_debounce_ms;   // 0 = Immediate
  bool cv_trigger_pressing;          // Runtime gate state
  
  // Velocity mode for continuous input note outputs
  velocity_mode_t expression_velocity_mode;  // For expression note output
  velocity_mode_t proximity_velocity_mode;   // For proximity note output
  velocity_mode_t als_velocity_mode;         // For ambient light note output
  velocity_mode_t tilt_x_velocity_mode;      // For tilt X note output
  velocity_mode_t tilt_y_velocity_mode;      // For tilt Y note output
  // OUTPUT_TYPE_TEMPO_NUDGE: percentage (0-100) of scene->bpm to swing
  // around the scene BPM; midi==64 returns exactly to scene->bpm.
  uint8_t tilt_x_tempo_nudge_pct;
  uint8_t tilt_y_tempo_nudge_pct;
  uint8_t expression_tempo_nudge_pct;
  uint8_t cv_tempo_nudge_pct;
  uint8_t proximity_tempo_nudge_pct;
  uint8_t touchwheel_tempo_nudge_pct;
  uint8_t als_tempo_nudge_pct;
  
  // Tempo configuration (per-scene)
  uint16_t bpm;                          // Tempo in beats per minute (20-300)
  tempo_clock_source_t clock_source;     // INTERNAL, MIDI, SYNC
  tempo_note_divider_t beat_divider;     // QUARTER, EIGHTH, SIXTEENTH
  time_signature_t time_signature;       // Beats per bar and beat unit
  bool use_transport;                    // If false, animation runs continuously at BPM
  bool send_clock;                       // If false, scene does not send MIDI clock
  
  // LFO configuration (per-scene)
  lfo_config_t lfo1_config;              // LFO1 waveform, rate, etc.
  lfo_config_t lfo2_config;              // LFO2 waveform, rate, etc.
  continuous_mapping_t lfo1;             // LFO1 output mapping (CC/note, curve, polarity)
  continuous_mapping_t lfo2;             // LFO2 output mapping (CC/note, curve, polarity)
  velocity_mode_t lfo1_velocity_mode;    // For LFO1 note output
  velocity_mode_t lfo2_velocity_mode;    // For LFO2 note output

  // RTG configuration (per-scene)
  rtg_config_t rtg_config;               // Random tone generator settings

  // Sample+Hold configuration (per-scene)
  sample_hold_config_t sample_hold_config;
  continuous_mapping_t sample_hold;      // S+H output mapping (CC, curve, polarity)
} scene_t;

// Scene cache entry
typedef struct {
  scene_t scene;
  uint8_t index;        // Which scene this is (0-127)
  bool valid;           // Whether this cache entry contains valid data
} scene_cache_entry_t;

// Scene manifest entry (lightweight metadata)
typedef struct {
  uint8_t index;
  char name[17];              // Scene name (max 16 chars + null)
  char filename[64];
  bool active;                // Whether scene participates in navigation
} scene_manifest_entry_t;

// Scene manager state
typedef struct {
  // Scene cache (current + neighbors for fast navigation)
  // Allocated from PSRAM to preserve internal RAM for SPI DMA
  scene_cache_entry_t* cache;
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
bool scene_name_is_reserved(const char* name);
esp_err_t scene_set_name(uint8_t scene_index, const char* name);
esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode);
esp_err_t scene_set_touchwheel_mode_runtime(uint8_t scene_index, touchwheel_mode_t mode);  // No persistence
touchwheel_mode_t scene_get_persisted_touchwheel_mode(uint8_t scene_index);  // Read from JSON
output_type_t scene_get_persisted_touchwheel_output_type(uint8_t scene_index);  // Read from JSON

// Set the touchwheel's internal value (used when switching CC parameters)
// This updates the endless encoder accumulator so the next CC send starts from this value
void scene_set_touchwheel_value(uint8_t value);

esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program);
esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc);

// UI module (per-scene screen)
esp_err_t scene_set_ui_module(uint8_t scene_index, const char* module_name);
const char* scene_get_ui_module(uint8_t scene_index);

// Device association (per-scene device targeting)
esp_err_t scene_set_device_id(uint8_t scene_index, const char* device_id);
const char* scene_get_device_id(uint8_t scene_index);
esp_err_t scene_clear_device_id(uint8_t scene_index);

// Per-scene MIDI channel (0 = use global, 1-16 = override)
esp_err_t scene_set_midi_channel(uint8_t scene_index, uint8_t channel);
uint8_t scene_get_midi_channel(uint8_t scene_index);

// Per-scene note output channel (0 = use scene channel, 1-16 = override)
esp_err_t scene_set_note_channel(uint8_t scene_index, uint8_t channel);
uint8_t scene_get_note_channel_setting(uint8_t scene_index);

// Per-scene TRS polarity (0 = use global, 1=A, 2=B, 3=TS, 4=Both)
esp_err_t scene_set_trs_type(uint8_t scene_index, uint8_t trs_type);
uint8_t scene_get_trs_type(uint8_t scene_index);

// Forward declaration for device_def_t (from assets_types.h)
struct device_def_t;

// Get the active device for a scene (loads device if needed)
// Returns device from scene's device_id, or global device_config if empty
// Caller must NOT free the returned pointer (managed internally)
const struct device_def_t* scene_get_device(uint8_t scene_index);

// Get the effective device slug for a scene (resolves to global if scene doesn't specify)
const char* scene_get_effective_device_slug(uint8_t scene_index);

// Get the effective MIDI channel for a scene (resolves to global if scene doesn't specify)
// Returns the channel to use (1-16) based on device_mode and scene settings
uint8_t scene_get_effective_channel(uint8_t scene_index);

// Get the effective TRS type for a scene (resolves to global if scene doesn't specify)
// Returns the TRS type to use based on device_mode and scene settings
midi_trs_type_t scene_get_effective_trs_type(uint8_t scene_index);

// Get the effective note output channel for a scene
// If note_channel is set (1-16), returns that; otherwise returns scene's effective channel
// Used by continuous stream sources in notes mode (touchwheel, expression, CV, proximity, ALS, LFO, RTG)
uint8_t scene_get_note_channel(uint8_t scene_index);

// Touchpad configuration
esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, 
                                uint8_t cc_number, uint8_t value);
esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled);
touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index);

// Action-based assignment API (single action per discrete input)
esp_err_t scene_assign_touchpad_action(uint8_t scene_index, uint8_t pad_index, const action_t* action);
esp_err_t scene_assign_button_left(uint8_t scene_index, const action_t* action);
esp_err_t scene_assign_button_right(uint8_t scene_index, const action_t* action);
esp_err_t scene_assign_button_both(uint8_t scene_index, const action_t* action);
esp_err_t scene_assign_bump(uint8_t scene_index, const action_t* action);
action_t* scene_get_button_left(uint8_t scene_index);
action_t* scene_get_button_right(uint8_t scene_index);
action_t* scene_get_button_both(uint8_t scene_index);
action_t* scene_get_bump(uint8_t scene_index);

// On-load actions (up to 4 actions per scene)
esp_err_t scene_add_on_load_action(uint8_t scene_index, const action_t* action);
esp_err_t scene_clear_on_load_actions(uint8_t scene_index);
uint8_t scene_get_num_on_load_actions(uint8_t scene_index);
action_t* scene_get_on_load_action(uint8_t scene_index, uint8_t action_index);

// On-play actions (up to 4 actions per scene, fire when transport starts playing)
esp_err_t scene_add_on_play_action(uint8_t scene_index, const action_t* action);
esp_err_t scene_clear_on_play_actions(uint8_t scene_index);
uint8_t scene_get_num_on_play_actions(uint8_t scene_index);
action_t* scene_get_on_play_action(uint8_t scene_index, uint8_t action_index);

// Pending change mode
uint8_t scene_get_pending_index(void);
bool scene_has_pending_change(void);
esp_err_t scene_confirm_change(void);
esp_err_t scene_cancel_pending(void);

// Process touchpad events through scene mappings
esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed);

// Expression jack mode and pedal assignment
esp_err_t scene_set_expression_mode(uint8_t scene_index, expression_mode_t mode);
expression_mode_t scene_get_expression_mode(uint8_t scene_index);
esp_err_t scene_assign_sustain(uint8_t scene_index, const action_t* action);
esp_err_t scene_assign_sostenuto(uint8_t scene_index, const action_t* action);
esp_err_t scene_assign_expr_switch(uint8_t scene_index, const action_t* action);
action_t* scene_get_sustain(uint8_t scene_index);
action_t* scene_get_sostenuto(uint8_t scene_index);
action_t* scene_get_expr_switch(uint8_t scene_index);

// CV input mode configuration
esp_err_t scene_set_cv_input_mode(uint8_t scene_index, input_mode_t mode);
input_mode_t scene_get_cv_input_mode(uint8_t scene_index);

// CV NOTE mode velocity configuration
esp_err_t scene_set_cv_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_cv_velocity_mode(uint8_t scene_index);
esp_err_t scene_set_cv_velocity(uint8_t scene_index, uint8_t velocity);
uint8_t scene_get_cv_velocity(uint8_t scene_index);

// CV Trigger mode configuration (when cv_input_mode = TRIGGER)
action_t* scene_get_cv_trigger_action(uint8_t scene_index);
esp_err_t scene_set_cv_trigger_threshold(uint8_t scene_index, uint8_t threshold);
uint8_t scene_get_cv_trigger_threshold(uint8_t scene_index);
esp_err_t scene_set_cv_trigger_debounce_ms(uint8_t scene_index, uint16_t debounce_ms);
uint16_t scene_get_cv_trigger_debounce_ms(uint8_t scene_index);

// Audio envelope follower configuration (when cv_input_mode = AUDIO)
esp_err_t scene_set_audio_range(uint8_t scene_index, cv_range_t range);
cv_range_t scene_get_audio_range(uint8_t scene_index);
esp_err_t scene_set_audio_sensitivity(uint8_t scene_index, uint8_t sensitivity);
uint8_t scene_get_audio_sensitivity(uint8_t scene_index);
esp_err_t scene_set_audio_attack(uint8_t scene_index, uint16_t attack_ms);
uint16_t scene_get_audio_attack(uint8_t scene_index);
esp_err_t scene_set_audio_release(uint8_t scene_index, uint16_t release_ms);
uint16_t scene_get_audio_release(uint8_t scene_index);
esp_err_t scene_set_audio_threshold(uint8_t scene_index, uint8_t threshold);
uint8_t scene_get_audio_threshold(uint8_t scene_index);
esp_err_t scene_set_audio_polarity(uint8_t scene_index, audio_polarity_t polarity);
audio_polarity_t scene_get_audio_polarity(uint8_t scene_index);
audio_config_t* scene_get_audio_config(uint8_t scene_index);

// Expression note output velocity mode
esp_err_t scene_set_expression_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_expression_velocity_mode(uint8_t scene_index);

// Proximity note output velocity mode
esp_err_t scene_set_proximity_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_proximity_velocity_mode(uint8_t scene_index);

// ALS note output velocity mode
esp_err_t scene_set_als_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_als_velocity_mode(uint8_t scene_index);

// Tilt X/Y note output velocity modes
esp_err_t scene_set_tilt_x_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_tilt_x_velocity_mode(uint8_t scene_index);
esp_err_t scene_set_tilt_y_velocity_mode(uint8_t scene_index, velocity_mode_t mode);
velocity_mode_t scene_get_tilt_y_velocity_mode(uint8_t scene_index);

// Tilt tempo nudge percentages (OUTPUT_TYPE_TEMPO_NUDGE)
esp_err_t scene_set_tilt_x_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_tilt_x_tempo_nudge_pct(uint8_t scene_index);
esp_err_t scene_set_tilt_y_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_tilt_y_tempo_nudge_pct(uint8_t scene_index);

// Tempo nudge percentages for other continuous sources (OUTPUT_TYPE_TEMPO_NUDGE)
esp_err_t scene_set_expression_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_expression_tempo_nudge_pct(uint8_t scene_index);
esp_err_t scene_set_cv_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_cv_tempo_nudge_pct(uint8_t scene_index);
esp_err_t scene_set_proximity_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_proximity_tempo_nudge_pct(uint8_t scene_index);
esp_err_t scene_set_touchwheel_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_touchwheel_tempo_nudge_pct(uint8_t scene_index);
esp_err_t scene_set_als_tempo_nudge_pct(uint8_t scene_index, uint8_t pct);
uint8_t scene_get_als_tempo_nudge_pct(uint8_t scene_index);

// Touchwheel velocity (when in TOUCHWHEEL_MODE_VELOCITY)
uint8_t scene_get_touchwheel_velocity(void);

// Touchwheel LFO rate (when in TOUCHWHEEL_MODE_LFO_RATE)
uint8_t scene_get_touchwheel_lfo_rate(void);

// Touchwheel LFO depth (when in TOUCHWHEEL_MODE_LFO_DEPTH)
uint8_t scene_get_touchwheel_lfo_depth(void);

// Touchwheel RTG rate (when in TOUCHWHEEL_MODE_RTG_RATE)
uint8_t scene_get_touchwheel_rtg_rate(void);

// External LFO rate sources (updated from sensor events)
uint8_t scene_get_expression_lfo_rate(void);
uint8_t scene_get_cv_lfo_rate(void);
uint8_t scene_get_als_lfo_rate(void);
uint8_t scene_get_proximity_lfo_rate(void);
uint8_t scene_get_tilt_x_lfo_rate(void);
uint8_t scene_get_tilt_y_lfo_rate(void);

// Tempo configuration (per-scene)
esp_err_t scene_set_bpm(uint8_t scene_index, uint16_t bpm);
uint16_t scene_get_bpm(uint8_t scene_index);
esp_err_t scene_set_clock_source(uint8_t scene_index, tempo_clock_source_t source);
tempo_clock_source_t scene_get_clock_source(uint8_t scene_index);
esp_err_t scene_set_beat_divider(uint8_t scene_index, tempo_note_divider_t divider);
tempo_note_divider_t scene_get_beat_divider(uint8_t scene_index);
esp_err_t scene_set_time_signature(uint8_t scene_index, uint8_t numerator, uint8_t denominator);
time_signature_t scene_get_time_signature(uint8_t scene_index);

// Transport mode (animation behavior)
esp_err_t scene_set_use_transport(uint8_t scene_index, bool use_transport);
bool scene_get_use_transport(uint8_t scene_index);

// Clock sending (per-scene)
esp_err_t scene_set_send_clock(uint8_t scene_index, bool send_clock);
bool scene_get_send_clock(uint8_t scene_index);

// Save/load scene mode configuration to/from NVS
esp_err_t scene_save_config(void);
esp_err_t scene_load_config(void);

// Scene storage (flash-based)
esp_err_t scene_load_from_flash(uint8_t scene_index);
esp_err_t scene_save_to_flash(uint8_t scene_index);

// Web CDC: full scene JSON read/write and reload from disk
bool scene_index_in_manifest(uint8_t scene_index);
esp_err_t scene_get_json(uint8_t scene_index, char **json_out);
esp_err_t scene_put_json(uint8_t scene_index, const char *json, size_t len);
esp_err_t scene_reload_index(uint8_t scene_index);
esp_err_t scene_load_manifest(void);
esp_err_t scene_save_manifest(void);
esp_err_t scene_rebuild_manifest_from_disk(bool reload_runtime);
esp_err_t scene_create_new(const char* name);
esp_err_t scene_create_new_at_position(const char* name, uint16_t position);
esp_err_t scene_delete(uint8_t scene_index);
esp_err_t scene_duplicate(uint8_t source_index, const char* new_name);

// Check if a scene name already exists (case-insensitive)
// Pass exclude_index >= 0 to skip a specific scene (e.g., when renaming)
bool scene_name_exists(const char* name, int8_t exclude_index);
esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index);
uint16_t scene_get_count(void);           // Active scenes only
uint16_t scene_get_total_count(void);      // All scenes (active + inactive)
// 1-based position among active scenes; returns false if scene_index not active
bool scene_get_active_slot(uint8_t scene_index, uint16_t *ordinal_1based,
  uint16_t *active_total);
bool scene_is_active(uint8_t scene_index);
esp_err_t scene_set_active(uint8_t scene_index, bool active);
const char* scene_get_name_by_position(uint16_t position);
uint8_t scene_get_index_by_position(uint16_t position);
bool scene_is_active_by_position(uint16_t position);

// Suspend/resume scene input processing (for programming mode)
// When suspended, the scene's touchwheel is unregistered and the LFO loops
// are paused (with their running state snapshotted for later restoration).
// MIDI output silencing is handled separately by midi_local_output_silence()
// -- query midi_local_output_is_enabled() to test "may producers emit".
esp_err_t scene_suspend_input(void);
esp_err_t scene_resume_input(void);

// Replay the MIDI phase (PC send, on-load actions, LFO start) that was
// deferred because a scene change occurred in programming mode.
// Called automatically when returning to performance mode.
void scene_apply_deferred_init(void);

// Clean up any active touchwheel notes (call when disabling touchwheel, changing modes, etc.)
void scene_touchwheel_cleanup_notes(void);

#endif // SCENE_H


