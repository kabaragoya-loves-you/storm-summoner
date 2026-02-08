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
  TOUCHWHEEL_MODE_LFO_RATE        // LFO rate modulation source (internal only)
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

// Scene structure
typedef struct {
  char name[17];              // Scene name (max 16 chars + null)
  char device_id[64];         // Device slug (empty = use global device_config)
  
  // Program change settings (modes 2 & 3)
  uint8_t program_number;     // PC value (0-127)
  bool send_pc_on_load;       // Send PC when loading this scene
  
  // Scene load actions (up to 4 actions executed when scene loads)
  uint8_t num_on_load_actions;
  action_t on_load[MAX_ON_LOAD_ACTIONS];
  
  // Touchwheel configuration
  touchwheel_mode_t touchwheel_mode;
  touchwheel_style_t touchwheel_style;  // For CONTINUOUS mode: odometer vs endless encoder
  continuous_mapping_t touchwheel;      // For TOUCHWHEEL_MODE_CONTINUOUS (like proximity/cv/etc)
  
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
  
  // Velocity mode for continuous input note outputs
  velocity_mode_t expression_velocity_mode;  // For expression note output
  velocity_mode_t proximity_velocity_mode;   // For proximity note output
  velocity_mode_t als_velocity_mode;         // For ambient light note output
  
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

// Validate all action timings in a scene against its time signature
// Call after scene load and when time signature changes
// (Defined in action.c but declared here to avoid circular includes)
void action_validate_scene_timings(scene_t* scene);

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
esp_err_t scene_set_touchwheel_mode_runtime(uint8_t scene_index, touchwheel_mode_t mode);  // No persistence
touchwheel_mode_t scene_get_persisted_touchwheel_mode(uint8_t scene_index);  // Read from JSON
output_type_t scene_get_persisted_touchwheel_output_type(uint8_t scene_index);  // Read from JSON
esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program);
esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc);

// Device association (per-scene device targeting)
esp_err_t scene_set_device_id(uint8_t scene_index, const char* device_id);
const char* scene_get_device_id(uint8_t scene_index);
esp_err_t scene_clear_device_id(uint8_t scene_index);

// Forward declaration for device_def_t (from assets_types.h)
struct device_def_t;

// Get the active device for a scene (loads device if needed)
// Returns device from scene's device_id, or global device_config if empty
// Caller must NOT free the returned pointer (managed internally)
const struct device_def_t* scene_get_device(uint8_t scene_index);

// Get the effective device slug for a scene (resolves to global if scene doesn't specify)
const char* scene_get_effective_device_slug(uint8_t scene_index);

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

// Touchwheel velocity (when in TOUCHWHEEL_MODE_VELOCITY)
uint8_t scene_get_touchwheel_velocity(void);

// Touchwheel LFO rate (when in TOUCHWHEEL_MODE_LFO_RATE)
uint8_t scene_get_touchwheel_lfo_rate(void);

// External LFO rate sources (updated from sensor events)
uint8_t scene_get_expression_lfo_rate(void);
uint8_t scene_get_cv_lfo_rate(void);
uint8_t scene_get_als_lfo_rate(void);
uint8_t scene_get_proximity_lfo_rate(void);

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
esp_err_t scene_load_manifest(void);
esp_err_t scene_save_manifest(void);
esp_err_t scene_create_new(const char* name);
esp_err_t scene_create_new_at_position(const char* name, uint16_t position);
esp_err_t scene_delete(uint8_t scene_index);
esp_err_t scene_duplicate(uint8_t source_index, const char* new_name);
esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index);
uint16_t scene_get_count(void);           // Active scenes only
uint16_t scene_get_total_count(void);      // All scenes (active + inactive)
bool scene_is_active(uint8_t scene_index);
esp_err_t scene_set_active(uint8_t scene_index, bool active);
const char* scene_get_name_by_position(uint16_t position);
uint8_t scene_get_index_by_position(uint16_t position);
bool scene_is_active_by_position(uint16_t position);

// Suspend/resume scene input processing (for programming mode)
// When suspended, the scene's touchwheel is unregistered and actions are disabled
esp_err_t scene_suspend_input(void);
esp_err_t scene_resume_input(void);
bool scene_is_input_suspended(void);

// Replay the MIDI phase (PC send, on-load actions, LFO start) that was
// deferred because a scene change occurred in programming mode.
// Called automatically when returning to performance mode.
void scene_apply_deferred_init(void);

// Clean up any active touchwheel notes (call when disabling touchwheel, changing modes, etc.)
void scene_touchwheel_cleanup_notes(void);

#endif // SCENE_H


