#include "scene.h"
#include "touchwheel.h"
#include "touch.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "device_config.h"
#include "assets_manager.h"
#include "config.h"
#include "app_settings.h"
#include "event_bus.h"
#include "action.h"
#include "tempo.h"
#include "input_manager.h"
#include "ui.h"
#include "memory_utils.h"
#include "lfo.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "scene";

// Scene storage paths
#define SCENES_BASE_PATH "/assets/scenes"
#define MANIFEST_PATH "/assets/scenes/manifest.json"

// Forward declarations
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size);
static esp_err_t json_to_scene(cJSON* root, scene_t* scene);
static void scene_init_defaults(scene_t* scene, uint8_t index);
static void scene_cleanup_touchwheel(void);
static void scene_setup_touchwheel_for_mode(const scene_t* scene);

// NVS keys
#define NVS_KEY_SCENE_MODE       "scene_mode"
#define NVS_KEY_CHANGE_MODE      "change_mode"

// Global scene manager instance
static scene_manager_t g_scene_manager = {
  .cache = NULL,  // Allocated from PSRAM in scene_init()
  .current_cache_idx = 0,
  .current_scene_index = 0,
  .pending_scene_index = 0,
  .has_pending_change = false,
  .manifest = NULL,
  .num_scenes = 1,
  .mode = SCENE_MODE_SINGLE,
  .change_mode = CHANGE_MODE_IMMEDIATE,
  .initialized = false
};

// Deferred init flag: when true, the MIDI phase of scene initialization
// (PC send, on-load actions, LFO start) was skipped because we were in
// programming mode. It will be replayed on return to performance mode.
static bool s_needs_deferred_init = false;

// Helper: Save current scene immediately if in programming mode
// Programming mode changes are always persisted; performance mode changes are temporary
static void scene_persist_if_programming(void) {
  if (ui_is_in_programming_mode()) {
    uint8_t scene_index = g_scene_manager.current_scene_index;
    esp_err_t ret = scene_save_to_flash(scene_index);
    if (ret == ESP_OK) {
      ESP_LOGD(TAG, "Scene %d saved (programming mode)", scene_index + 1);
    } else {
      ESP_LOGW(TAG, "Failed to save scene %d: %s", scene_index + 1, esp_err_to_name(ret));
    }
  }
}

// Touchwheel instance for scene encoder mode
static touchwheel_instance_t* s_scene_touchwheel = NULL;
static bool s_input_suspended = false;  // True when in programming mode

// Pitch bend return-to-center animation (declared early for cleanup function)
static esp_timer_handle_t s_pitch_bend_timer = NULL;

// Cached device definition for current scene
static device_def_t* s_cached_device = NULL;
static char s_cached_device_slug[64] = "";

// Default CC assignments for initial testing
static const uint8_t DEFAULT_CC_NUMBERS[NUM_TOUCHPADS] = {
  1, 2, 3, 4, 5, 6, 7, 8,     // Touchwheel pads (0-7)
  9, 10, 11, 12               // Additional pads (8-11)
};

// Helper: Get scene by index (returns current scene if it matches, otherwise error)
// For now, we only allow modifications to the current scene
static scene_t* get_scene_for_modification(uint8_t scene_index) {
  if (scene_index != g_scene_manager.current_scene_index) {
    ESP_LOGW(TAG, "Can only modify current scene (currently on %d, requested %d)", 
             g_scene_manager.current_scene_index, scene_index);
    return NULL;
  }
  
  return scene_get_current();
}

// Set default button assignments based on scene mode
static void set_default_button_assignments(scene_t* scene) {
  scene_mode_t mode = g_scene_manager.mode;
  
  if (mode == SCENE_MODE_SINGLE) {
    // Mode 1: Buttons control program changes
    scene->button_left = action_create_preset_dec();
    scene->button_right = action_create_preset_inc();
    scene->button_both.type = ACTION_CONFIRM_PENDING;
  } else {
    // Modes 2 & 3: Buttons control scene navigation
    scene->button_left = action_create_scene_dec();
    scene->button_right = action_create_scene_inc();
    scene->button_both.type = ACTION_CONFIRM_PENDING;
  }
}

// Initialize a single scene with defaults
static void scene_init_defaults(scene_t* scene, uint8_t index) {
  memset(scene, 0, sizeof(scene_t));
  
  // Set default name
  snprintf(scene->name, sizeof(scene->name), "Scene %d", index + 1);
  
  // Program change defaults
  scene->program_number = index;  // Match scene index in preset sync mode
  scene->send_pc_on_load = true;
  
  // Default touchwheel mode
  scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;
  scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;  // Default: position-based (~15 values)
  scene->touchwheel = continuous_mapping_create(16);    // CC16 = General Purpose 1
  scene->touchwheel.enabled = false;                    // Disabled by default (BUTTONS mode)
  
  // Initialize touchpad mappings with default CC actions
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    scene->touchpads[i].enabled = true;
    scene->touchpads[i].action = action_create_control(DEFAULT_CC_NUMBERS[i], 127);
  }
  
  // Set default button assignments
  set_default_button_assignments(scene);
  
  // Initialize on_load actions (default: reset for clean slate)
  scene->num_on_load_actions = 1;
  scene->on_load[0] = action_create_reset();
  
  // Initialize discrete trigger inputs
  scene->bump = action_create_tap_tempo();  // Default: tap tempo on bump
  
  // Initialize continuous input mappings with defaults
  scene->expression = continuous_mapping_create(4);    // CC4 = Foot Controller (expression pedal)
  scene->cv = continuous_mapping_create(16);           // CC16 = General Purpose 1
  scene->proximity = continuous_mapping_create(17);    // CC17 = General Purpose 2
  scene->proximity.use_idle_value = true;              // Proximity returns to center
  scene->proximity.idle_value = 64;                    // Center for CC (60 for NOTE mode)
  scene->proximity.idle_timeout_ms = 1000;
  scene->proximity.polarity = POLARITY_BIPOLAR;
  scene->als = continuous_mapping_create(18);          // CC18 = General Purpose 3
  
  // Expression jack configuration
  scene->expression_mode = EXPRESSION_MODE_PEDAL;      // Default to expression pedal mode
  
  // Default sustain action: ACTION_SUSTAIN (CC64 toggle)
  scene->sustain = action_create_sustain();
  
  // Default sostenuto action: ACTION_SOSTENUTO (CC66 toggle)
  scene->sostenuto = action_create_sostenuto();
  
  // CV input configuration
  scene->cv_input_mode = INPUT_MODE_CV;                // Default to CV mode
  
  // CV NOTE mode velocity configuration
  scene->cv_velocity_mode = VELOCITY_MODE_FIXED;       // Default to fixed velocity
  scene->cv_velocity = 100;                            // Default velocity value
  
  // Audio envelope follower defaults (when cv_input_mode = AUDIO)
  scene->audio_config.range = CV_RANGE_BIPOLAR_5V;     // Default to ±5V for audio
  scene->audio_config.sensitivity = 128;              // 1.0x gain (middle)
  scene->audio_config.attack_ms = 10;                 // Fast attack for transients
  scene->audio_config.release_ms = 200;               // Medium release
  scene->audio_config.threshold = 5;                  // Small noise gate
  scene->audio_config.polarity = AUDIO_POLARITY_ATTRACT;  // Louder = higher value
  
  // Velocity modes for other continuous inputs (all default to fixed)
  scene->expression_velocity_mode = VELOCITY_MODE_FIXED;
  scene->proximity_velocity_mode = VELOCITY_MODE_FIXED;
  scene->als_velocity_mode = VELOCITY_MODE_FIXED;
  
  // Tempo configuration
  scene->bpm = 120;                                    // Default to 120 BPM
  scene->clock_source = CLOCK_SOURCE_INTERNAL;         // Default to internal clock
  scene->beat_divider = DIVIDER_QUARTER;               // Default to quarter note beats
  scene->time_signature.numerator = 4;                 // Default to 4/4 time
  scene->time_signature.denominator = 4;
  scene->use_transport = false;                        // Default: animation always runs
  scene->send_clock = true;                            // Default: send MIDI clock
  
  // LFO configuration
  scene->lfo1_config = lfo_config_create_default();
  scene->lfo2_config = lfo_config_create_default();
  scene->lfo1 = continuous_mapping_create(80);         // CC80 = General Purpose 5
  scene->lfo1.enabled = false;                         // Disabled by default
  scene->lfo2 = continuous_mapping_create(81);         // CC81 = General Purpose 6
  scene->lfo2.enabled = false;                         // Disabled by default
  scene->lfo1_velocity_mode = VELOCITY_MODE_FIXED;
  scene->lfo2_velocity_mode = VELOCITY_MODE_FIXED;
}

// Cleanup existing touchwheel instance
static void scene_cleanup_touchwheel(void) {
  // Stop and delete pitch bend return timer
  if (s_pitch_bend_timer) {
    esp_timer_stop(s_pitch_bend_timer);
    esp_timer_delete(s_pitch_bend_timer);
    s_pitch_bend_timer = NULL;
  }
  
  if (s_scene_touchwheel) {
    touch_unregister_touchwheel_instance(s_scene_touchwheel);
    touchwheel_destroy(s_scene_touchwheel);
    s_scene_touchwheel = NULL;
  }
}

// Callback for program change mode touchwheel
static void touchwheel_program_change_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  (void)user_data;
  
  // value is delta from endless encoder (+1, -1, etc.)
  if (value == 0) return;
  
  // Check if bank mode is enabled for extended preset range
  bank_select_mode_t bank_mode = device_config_get_bank_mode();
  uint16_t min_preset = device_config_get_min_preset();
  uint16_t max_preset = device_config_get_max_preset();
  
  ESP_LOGD(TAG, "PC touchwheel: bank_mode=%d, min=%u, max=%u, wrap=%d, count=%u",
    bank_mode, (unsigned)min_preset, (unsigned)max_preset,
    config_get_preset_wrap(), (unsigned)device_config_get_preset_count());
  
  if (bank_mode != BANK_SELECT_NONE) {
    // Bank mode: use preset-based calculation, respecting device preset count and indexBase
    uint16_t base_preset = device_config_has_pending_program()
                           ? device_config_get_pending_preset()
                           : device_config_get_preset();
    int new_preset = (int)base_preset + value;
    
    // Clamp or wrap at boundaries based on wrap setting
    bool wrap = config_get_preset_wrap();
    int range = (int)max_preset - (int)min_preset + 1;
    if (wrap) {
      // Wrap around within valid range
      while (new_preset < (int)min_preset) new_preset += range;
      while (new_preset > (int)max_preset) new_preset -= range;
    } else {
      // Clamp at boundaries (no wrap)
      if (new_preset < (int)min_preset) new_preset = (int)min_preset;
      if (new_preset > (int)max_preset) new_preset = (int)max_preset;
    }
    
    if ((uint16_t)new_preset == base_preset) return;
    
    // Respect immediate/pending mode
    if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
      device_config_set_preset((uint16_t)new_preset);
      ESP_LOGD(TAG, "Touchwheel preset change: %u -> %d (bank %d, prog %d)", 
               (unsigned)base_preset, new_preset, new_preset / 128, new_preset % 128);
    } else {
      device_config_set_pending_preset((uint16_t)new_preset);
      ESP_LOGI(TAG, "Touchwheel pending preset: %d (bank %d, prog %d)", 
               new_preset, new_preset / 128, new_preset % 128);
    }
    return;
  }
  
  // No bank mode: respect indexBase, cap at 127
  uint8_t min_prog = (uint8_t)min_preset;
  uint8_t max_prog = (max_preset > 127) ? 127 : (uint8_t)max_preset;
  uint8_t base = device_config_has_pending_program() 
                 ? device_config_get_pending_program() 
                 : device_config_get_program();
  int new_program = (int)base + value;
  
  bool should_clamp = !config_get_preset_wrap();
  int range = (int)max_prog - (int)min_prog + 1;
  
  if (should_clamp) {
    // Clamp at boundaries (no wrap)
    if (new_program < (int)min_prog) new_program = (int)min_prog;
    if (new_program > (int)max_prog) new_program = (int)max_prog;
  } else {
    // Wrap around within valid range (only if not locked)
    while (new_program < (int)min_prog) new_program += range;
    while (new_program > (int)max_prog) new_program -= range;
  }
  
  if ((uint8_t)new_program == base) return;
  
  // Respect immediate/pending mode
  if (device_config_get_pc_mode() == PC_MODE_IMMEDIATE) {
    device_config_set_program((uint8_t)new_program);
    ESP_LOGD(TAG, "Touchwheel program change: %d -> %d", base, new_program);
  } else {
    // Set as pending - will be confirmed by confirm_pending action
    device_config_set_pending_program((uint8_t)new_program);
    ESP_LOGI(TAG, "Touchwheel pending program: %d (confirm to send)", new_program);
  }
}

// Tracked value for endless encoder continuous mode
static int s_touchwheel_endless_value = 64;  // Start at center
static int s_touchwheel_last_sent_cc = -1;   // Last sent CC value (-1 = none sent yet)

// Tracked values for new touchwheel modes
static int s_touchwheel_tempo_bpm = 120;           // BPM 20-300
static int s_touchwheel_pitch_bend = 0;            // -8192 to 8191, center at 0
static int s_touchwheel_prev_pitch_bend = 0;       // Previous value for smooth interpolation
static int s_touchwheel_aftertouch = 0;            // 0-127
static int s_touchwheel_14bit_value = 0;           // For NRPN/RPN/DoubleCC (0-16383)
static volatile uint8_t s_touchwheel_velocity = 100; // For TOUCHWHEEL_MODE_VELOCITY
static volatile uint8_t s_touchwheel_lfo_rate = 64;   // For TOUCHWHEEL_MODE_LFO_RATE (64 = center/default)

// External LFO rate sources (updated from sensor events)
static volatile uint8_t s_expression_lfo_rate = 64;
static volatile uint8_t s_cv_lfo_rate = 64;
static volatile uint8_t s_als_lfo_rate = 64;
static volatile uint8_t s_proximity_lfo_rate = 64;

// Pitch bend return-to-center animation
// Precalculated eased return sequences for each distance bucket
// Uses ease-out curve (fast start, slow finish) for natural feel
#define PB_ANIM_INTERVAL_US  10000  // 10ms between frames (100Hz)

// Return sequences for 7 distance buckets (1=closest to center, 7=furthest)
// Each sequence eases from the bucket's value down to 0
static const int16_t pb_return_1[] = {1024, 0};  // 2 frames
static const int16_t pb_return_2[] = {1600, 800, 0};  // 3 frames
static const int16_t pb_return_3[] = {2800, 1800, 900, 0};  // 4 frames
static const int16_t pb_return_4[] = {4200, 3000, 1800, 800, 0};  // 5 frames
static const int16_t pb_return_5[] = {5400, 4200, 3000, 1900, 900, 0};  // 6 frames
static const int16_t pb_return_6[] = {6400, 5200, 4000, 2800, 1700, 800, 0};  // 7 frames
static const int16_t pb_return_7[] = {7200, 6000, 4800, 3600, 2500, 1500, 700, 0};  // 8 frames

static const int16_t* pb_return_sequences[] = {
  pb_return_1, pb_return_2, pb_return_3, pb_return_4,
  pb_return_5, pb_return_6, pb_return_7
};
static const uint8_t pb_return_lengths[] = {2, 3, 4, 5, 6, 7, 8};

// Animation state
static const int16_t* s_pb_anim_sequence = NULL;  // Current sequence being played
static uint8_t s_pb_anim_length = 0;              // Length of current sequence
static uint8_t s_pb_anim_index = 0;               // Current frame index
static bool s_pb_anim_negative = false;           // True if returning from negative value

// Continuous note mode state - supports polyphony up to 4 voices
typedef struct {
  uint8_t note;
  bool active;
  esp_timer_handle_t release_timer;  // For latch mode delayed release
} poly_voice_t;

static poly_voice_t s_poly_voices[MAX_POLY_VOICES] = {0};
static uint8_t s_current_voice_idx = 0;  // Round-robin for poly mode

// Legacy mono mode state (for compatibility)
static bool s_touchwheel_note_active = false;
static uint8_t s_touchwheel_current_note = 0;

// Forward declaration for timer callback
static void latch_release_timer_cb(void* arg);

// Get the next available voice for poly mode
static int get_next_poly_voice(uint8_t note) {
  // First, check if this note is already playing
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (s_poly_voices[i].active && s_poly_voices[i].note == note) {
      return i;  // Retrigger same voice
    }
  }
  // Find an inactive voice
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (!s_poly_voices[i].active) return i;
  }
  // All voices active, steal oldest (round-robin)
  int victim = s_current_voice_idx;
  s_current_voice_idx = (s_current_voice_idx + 1) % MAX_POLY_VOICES;
  return victim;
}

// Clean up any active touchwheel notes (call before mode changes, suspend, disable, etc.)
static void touchwheel_cleanup_active_notes(void) {
  uint8_t channel = device_config_get_channel() - 1;
  
  // Clean up poly voices
  for (int i = 0; i < MAX_POLY_VOICES; i++) {
    if (s_poly_voices[i].active) {
      send_note_off(channel, s_poly_voices[i].note, 0);
      ESP_LOGI(TAG, "Touchwheel Note OFF (cleanup): %d", s_poly_voices[i].note);
      s_poly_voices[i].active = false;
    }
    if (s_poly_voices[i].release_timer) {
      esp_timer_stop(s_poly_voices[i].release_timer);
    }
  }
  
  // Clean up mono state
  if (s_touchwheel_note_active) {
    send_note_off(channel, s_touchwheel_current_note, 0);
    ESP_LOGI(TAG, "Touchwheel Note OFF (cleanup): %d", s_touchwheel_current_note);
    s_touchwheel_note_active = false;
  }
}

// Public wrapper for cleanup (called from console commands, etc.)
void scene_touchwheel_cleanup_notes(void) {
  touchwheel_cleanup_active_notes();
}

// Timer callback for latch mode delayed note release
static void latch_release_timer_cb(void* arg) {
  int voice_idx = (int)(intptr_t)arg;
  if (voice_idx < 0 || voice_idx >= MAX_POLY_VOICES) return;
  
  poly_voice_t* voice = &s_poly_voices[voice_idx];
  if (voice->active) {
    uint8_t channel = device_config_get_channel() - 1;
    send_note_off(channel, voice->note, 0);
    ESP_LOGD(TAG, "Latch release: Note OFF %d (voice %d)", voice->note, voice_idx);
    voice->active = false;
  }
}

// Release callback for continuous mode touchwheel (note off)
static void touchwheel_continuous_release_callback(void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  
  // In latch mode, don't release immediately - start timers
  if (scene && scene->touchwheel.note_latch) {
    uint16_t release_ms = scene->touchwheel.note_release_ms;
    if (release_ms < 100) release_ms = 500;  // Default
    
    // Start release timers for all active voices
    for (int i = 0; i < MAX_POLY_VOICES; i++) {
      if (s_poly_voices[i].active) {
        // Create timer if needed
        if (!s_poly_voices[i].release_timer) {
          esp_timer_create_args_t args = {
            .callback = latch_release_timer_cb,
            .arg = (void*)(intptr_t)i,
            .name = "latch_rel"
          };
          esp_timer_create(&args, &s_poly_voices[i].release_timer);
        }
        esp_timer_stop(s_poly_voices[i].release_timer);
        esp_timer_start_once(s_poly_voices[i].release_timer, release_ms * 1000);
      }
    }
    
    // Handle mono mode latch
    if (s_touchwheel_note_active) {
      // For mono, just use voice 0's timer
      if (!s_poly_voices[0].release_timer) {
        esp_timer_create_args_t args = {
          .callback = latch_release_timer_cb,
          .arg = (void*)(intptr_t)0,
          .name = "latch_rel"
        };
        esp_timer_create(&args, &s_poly_voices[0].release_timer);
      }
      s_poly_voices[0].note = s_touchwheel_current_note;
      s_poly_voices[0].active = true;
      s_touchwheel_note_active = false;  // Transfer to poly tracking
      esp_timer_stop(s_poly_voices[0].release_timer);
      esp_timer_start_once(s_poly_voices[0].release_timer, release_ms * 1000);
    }
    return;
  }
  
  // Non-latch mode: immediate release
  touchwheel_cleanup_active_notes();
}

// Callback for set_tempo mode touchwheel
static void touchwheel_tempo_callback(int value, void* user_data) {
  // Don't change tempo in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  (void)scene;  // May use for style check later
  
  if (value == 0) return;
  
  s_touchwheel_tempo_bpm += value;
  // Clamp to 20-300 BPM
  if (s_touchwheel_tempo_bpm < 20) s_touchwheel_tempo_bpm = 20;
  if (s_touchwheel_tempo_bpm > 300) s_touchwheel_tempo_bpm = 300;
  
  tempo_set_bpm((uint16_t)s_touchwheel_tempo_bpm);
  ESP_LOGD(TAG, "Touchwheel tempo: %d BPM", s_touchwheel_tempo_bpm);
}

// Callback for pitch bend mode touchwheel (true bipolar - position maps directly to value)
static void touchwheel_pitch_bend_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  (void)user_data;
  
  // Stop any running return animation when finger touches
  if (s_pitch_bend_timer) {
    esp_timer_stop(s_pitch_bend_timer);
  }
  
  // Bipolar mode: value is position from -100 to +100 (approx)
  // Scale to pitch bend range -8192 to +8191
  int new_pitch_bend = (value * 8192) / 100;
  
  // Clamp to valid range
  if (new_pitch_bend < -8192) new_pitch_bend = -8192;
  if (new_pitch_bend > 8191) new_pitch_bend = 8191;
  
  uint8_t channel = device_config_get_channel() - 1;
  
  // Calculate delta from previous position
  int delta = new_pitch_bend - s_touchwheel_prev_pitch_bend;
  if (delta < 0) delta = -delta;
  
  // Smooth interpolation for large jumps
  if (delta > 800) {
    // Determine number of intermediate steps based on delta size
    int steps = (delta > 4000) ? 5 : (delta > 2500) ? 4 : (delta > 1500) ? 3 : 2;
    
    // Send intermediate values
    for (int i = 1; i < steps; i++) {
      int interp = s_touchwheel_prev_pitch_bend + 
        ((new_pitch_bend - s_touchwheel_prev_pitch_bend) * i) / steps;
      send_pitch_bend(channel, (int16_t)interp);
    }
  }
  
  // Send final value and update state
  s_touchwheel_pitch_bend = new_pitch_bend;
  s_touchwheel_prev_pitch_bend = new_pitch_bend;
  send_pitch_bend(channel, (int16_t)s_touchwheel_pitch_bend);
  ESP_LOGD(TAG, "Touchwheel pitch bend: %d", s_touchwheel_pitch_bend);
}

// Timer callback for pitch bend return-to-center animation (table-driven)
static void pitch_bend_return_timer_cb(void* arg) {
  (void)arg;
  
  // Safety check
  if (!s_pb_anim_sequence || s_pb_anim_index >= s_pb_anim_length) {
    s_touchwheel_pitch_bend = 0;
    s_touchwheel_prev_pitch_bend = 0;
    uint8_t channel = device_config_get_channel() - 1;
    send_pitch_bend(channel, 0);
    esp_timer_stop(s_pitch_bend_timer);
    return;
  }
  
  // Get the next value from the sequence
  int16_t value = s_pb_anim_sequence[s_pb_anim_index];
  
  // Apply sign if returning from negative
  s_touchwheel_pitch_bend = s_pb_anim_negative ? -value : value;
  
  // Send the pitch bend
  uint8_t channel = device_config_get_channel() - 1;
  send_pitch_bend(channel, (int16_t)s_touchwheel_pitch_bend);
  
  // Advance to next frame
  s_pb_anim_index++;
  
  // Stop when we've played all frames (last frame is always 0)
  if (s_pb_anim_index >= s_pb_anim_length) {
    esp_timer_stop(s_pitch_bend_timer);
    s_pb_anim_sequence = NULL;
    s_touchwheel_prev_pitch_bend = 0;  // Reset for next touch
    ESP_LOGD(TAG, "Pitch bend return complete");
  }
}

// Release callback for pitch bend - starts return-to-center animation
static void touchwheel_pitch_bend_release_callback(void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  (void)user_data;
  
  // Already at center, nothing to do
  if (s_touchwheel_pitch_bend == 0) return;
  
  // Create timer if needed
  if (!s_pitch_bend_timer) {
    const esp_timer_create_args_t timer_args = {
      .callback = pitch_bend_return_timer_cb,
      .name = "pb_return"
    };
    if (esp_timer_create(&timer_args, &s_pitch_bend_timer) != ESP_OK) {
      // Fallback: immediate return
      s_touchwheel_pitch_bend = 0;
      uint8_t channel = device_config_get_channel() - 1;
      send_pitch_bend(channel, 0);
      return;
    }
  }
  
  // Determine if negative and get absolute value
  int abs_value = s_touchwheel_pitch_bend;
  s_pb_anim_negative = (abs_value < 0);
  if (abs_value < 0) abs_value = -abs_value;
  
  // Map absolute value to bucket (0-6)
  // Buckets: 0=0-1170, 1=1171-2340, 2=2341-3510, 3=3511-4680,
  //          4=4681-5850, 5=5851-7020, 6=7021-8192
  int bucket = (abs_value * 7) / 8192;
  if (bucket > 6) bucket = 6;
  
  // Select the return sequence for this bucket
  s_pb_anim_sequence = pb_return_sequences[bucket];
  s_pb_anim_length = pb_return_lengths[bucket];
  s_pb_anim_index = 0;
  
  ESP_LOGD(TAG, "Pitch bend return: from %d, bucket %d, %d frames",
    s_touchwheel_pitch_bend, bucket, s_pb_anim_length);
  
  // Start the periodic timer
  esp_timer_start_periodic(s_pitch_bend_timer, PB_ANIM_INTERVAL_US);
}

// Callback for channel aftertouch mode touchwheel
static void touchwheel_aftertouch_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  
  uint8_t midi_value;
  
  if (scene && scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is speed-multiplied delta (can be up to ±20)
    // Scale so ~2 rotations at normal speed = full range
    // 2 rotations * 8 steps = 16 steps, 127/16 ≈ 8
    int scaled_delta = value * 8;
    s_touchwheel_aftertouch += scaled_delta;
    if (s_touchwheel_aftertouch < 0) s_touchwheel_aftertouch = 0;
    if (s_touchwheel_aftertouch > 127) s_touchwheel_aftertouch = 127;
    midi_value = (uint8_t)s_touchwheel_aftertouch;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    midi_value = (uint8_t)((value * 127) / 100);
    s_touchwheel_aftertouch = midi_value;
  }
  
  uint8_t channel = device_config_get_channel() - 1;
  send_channel_aftertouch(channel, midi_value);
  ESP_LOGD(TAG, "Touchwheel aftertouch: %d", midi_value);
}

// Callback for double CC mode touchwheel (14-bit CC, MSB=cc_numbers[0], LSB=cc_numbers[0]+32)
static void touchwheel_double_cc_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  if (!scene) return;

  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is speed-multiplied delta
    // Scale so ~4 rotations at normal speed = full 14-bit range
    // Use 500 (not a multiple of 128) so both MSB and LSB visibly change
    s_touchwheel_14bit_value += value * 500;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-16383
    s_touchwheel_14bit_value = (value * 16383) / 100;
  }

  if (s_touchwheel_14bit_value < 0) s_touchwheel_14bit_value = 0;
  if (s_touchwheel_14bit_value > 16383) s_touchwheel_14bit_value = 16383;

  uint8_t channel = device_config_get_channel() - 1;
  uint8_t msb_cc = scene->touchwheel.num_cc_numbers > 0 ? scene->touchwheel.cc_numbers[0] : 0;
  uint8_t lsb_cc = msb_cc + 32;  // Standard 14-bit CC: LSB = MSB + 32
  send_double_control_change(channel, msb_cc, lsb_cc, (uint16_t)s_touchwheel_14bit_value);
  ESP_LOGD(TAG, "Touchwheel DoubleCC[%d/%d]: %d (MSB=%d, LSB=%d)", 
    msb_cc, lsb_cc, s_touchwheel_14bit_value,
    (s_touchwheel_14bit_value >> 7) & 0x7F, s_touchwheel_14bit_value & 0x7F);
}

// Callback for velocity mode touchwheel (internal only - no MIDI output)
static void touchwheel_velocity_callback(int value, void* user_data) {
  (void)user_data;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  uint8_t new_velocity;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_velocity;
    current += value * 4;  // Scale delta for reasonable speed
    if (current < 1) current = 1;
    if (current > 127) current = 127;
    new_velocity = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 1-127
    new_velocity = 1 + (value * 126) / 100;
    if (new_velocity > 127) new_velocity = 127;
  }
  
  s_touchwheel_velocity = new_velocity;
  ESP_LOGD(TAG, "Touchwheel velocity: %d", new_velocity);
}

// Callback for LFO rate mode touchwheel (internal only - modulates LFO speed)
static void touchwheel_lfo_rate_callback(int value, void* user_data) {
  (void)user_data;

  scene_t* scene = scene_get_current();
  if (!scene) return;

  uint8_t new_rate;
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is delta, accumulate
    int current = (int)s_touchwheel_lfo_rate;
    current += value * 2;  // Scale delta
    if (current < 0) current = 0;
    if (current > 127) current = 127;
    new_rate = (uint8_t)current;
  } else {
    // Odometer mode: value is 0-100%, scale to 0-127
    new_rate = (value * 127) / 100;
    if (new_rate > 127) new_rate = 127;
  }

  s_touchwheel_lfo_rate = new_rate;
  ESP_LOGD(TAG, "Touchwheel LFO rate: %d", new_rate);
}

// Callback for continuous mode touchwheel (CC/Note output)
static void touchwheel_continuous_callback(int value, void* user_data) {
  // Don't send MIDI in programming mode
  if (ui_is_in_programming_mode()) return;
  
  scene_t* scene = (scene_t*)user_data;
  if (!scene || !scene->touchwheel.enabled) return;
  
  // Get device for discrete value handling
  const device_def_t* device = s_cached_device;
  uint8_t cc_num = scene->touchwheel.cc_number;
  bool has_discrete = (device && scene->touchwheel.output_type == OUTPUT_TYPE_CC &&
    assets_cc_has_discrete_values(device, cc_num));
  
  uint8_t midi_value;
  
  if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
    // Endless mode: value is a delta (+1, -1, etc.)
    if (has_discrete) {
      // For discrete CCs, cycle through discrete values
      if (value > 0) {
        s_touchwheel_endless_value = assets_get_next_discrete(device, cc_num, s_touchwheel_endless_value);
      } else if (value < 0) {
        s_touchwheel_endless_value = assets_get_prev_discrete(device, cc_num, s_touchwheel_endless_value);
      }
      midi_value = (uint8_t)s_touchwheel_endless_value;
    } else {
      // Standard continuous: increment by delta
      s_touchwheel_endless_value += value;
      // Clamp to device min/max or 0-127
      int min_val = 0, max_val = 127;
      if (device) {
        const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
        if (ctrl) {
          min_val = ctrl->min;
          max_val = ctrl->max;
        }
      }
      if (s_touchwheel_endless_value < min_val) s_touchwheel_endless_value = min_val;
      if (s_touchwheel_endless_value > max_val) s_touchwheel_endless_value = max_val;
      midi_value = (uint8_t)s_touchwheel_endless_value;
    }
  } else {
    // Odometer mode: value is 0-100%, scale to appropriate range
    if (has_discrete) {
      // Map odometer position to discrete value index
      const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
      if (ctrl && ctrl->discrete_count > 0) {
        int idx = (value * (ctrl->discrete_count - 1)) / 100;
        if (idx >= ctrl->discrete_count) idx = ctrl->discrete_count - 1;
        if (idx < 0) idx = 0;
        midi_value = ctrl->discrete_values[idx].value;
        s_touchwheel_endless_value = midi_value;  // Update for display
      } else {
        midi_value = (uint8_t)((value * 127) / 100);
      }
    } else {
      // Scale to device min/max range or 0-127
      int min_val = 0, max_val = 127;
      if (device) {
        const midi_control_t* ctrl = assets_get_control_by_cc(device, cc_num);
        if (ctrl) {
          min_val = ctrl->min;
          max_val = ctrl->max;
        }
      }
      midi_value = (uint8_t)(min_val + ((value * (max_val - min_val)) / 100));
    }
  }
  
  // Process through continuous mapping (applies curve, polarity, scaling)
  uint8_t output = continuous_mapping_process(midi_value, &scene->touchwheel);
  
  // Send MIDI based on output type
  uint8_t channel = device_config_get_channel() - 1;

  if (scene->touchwheel.output_type == OUTPUT_TYPE_CC) {
    // Don't send if no CCs are configured (neither multi-CC slots nor single CC)
    if (scene->touchwheel.num_cc_numbers == 0 && scene->touchwheel.cc_number == 0) {
      return;
    }
    
    // Skip sending if value hasn't changed (prevents spam at min/max)
    if ((int)output == s_touchwheel_last_sent_cc) {
      return;
    }
    s_touchwheel_last_sent_cc = (int)output;
    
    continuous_mapping_send_cc(&scene->touchwheel, channel, output);
    if (has_discrete) {
      const char* name = assets_get_discrete_name(device, cc_num, output);
      ESP_LOGD(TAG, "Touchwheel CC%d = %d (%s)", cc_num, output, name ? name : "");
    } else {
      ESP_LOGD(TAG, "Touchwheel CC = %d", output);
    }
  } else {
    // Note mode: convert value to note number and play
    uint8_t note = continuous_mapping_value_to_note(output, &scene->touchwheel);
    uint8_t vel = scene->touchwheel.velocity;
    if (vel == 0) vel = 100;
    
    if (scene->touchwheel.polyphony == POLYPHONY_POLY) {
      // Polyphonic mode: each position can trigger a new voice
      int voice_idx = get_next_poly_voice(note);
      poly_voice_t* voice = &s_poly_voices[voice_idx];
      
      // Stop any pending release timer for this voice
      if (voice->release_timer) {
        esp_timer_stop(voice->release_timer);
      }
      
      // If voice was playing a different note, turn it off
      if (voice->active && voice->note != note) {
        send_note_off(channel, voice->note, 0);
      }
      
      // If this note isn't already playing on this voice, send note on
      if (!voice->active || voice->note != note) {
        send_note_on(channel, note, vel);
        ESP_LOGD(TAG, "Poly Note ON: %d vel=%d (voice %d)", note, vel, voice_idx);
      }
      
      voice->note = note;
      voice->active = true;
    } else {
      // Mono mode: single voice
      if (!s_touchwheel_note_active) {
        // First touch - send note on
        send_note_on(channel, note, vel);
        s_touchwheel_note_active = true;
        s_touchwheel_current_note = note;
        ESP_LOGD(TAG, "Touchwheel Note ON: %d vel=%d", note, vel);
      } else if (note != s_touchwheel_current_note) {
        // Note changed while holding - retrigger
        send_note_off(channel, s_touchwheel_current_note, 0);
        send_note_on(channel, note, vel);
        s_touchwheel_current_note = note;
        ESP_LOGD(TAG, "Touchwheel Note change: %d -> %d", s_touchwheel_current_note, note);
      }
    }
  }
}

// Setup touchwheel instance based on scene mode
static void scene_setup_touchwheel_for_mode(const scene_t* scene) {
  if (!scene) return;
  
  touchwheel_mode_processor_t* mode_proc = NULL;
  touchwheel_output_t* output = NULL;
  const char* mode_desc = NULL;
  
  switch (scene->touchwheel_mode) {
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE:
      mode_proc = touchwheel_mode_create_endless();
      output = touchwheel_output_callback_create(touchwheel_program_change_callback, NULL);
      mode_desc = "program_change";
      break;
      
    case TOUCHWHEEL_MODE_CONTINUOUS:
      // Choose between odometer and endless based on style setting
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "continuous (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "continuous (odometer)";
      }
      output = touchwheel_output_callback_create(touchwheel_continuous_callback, (void*)scene);
      // Register release callback for note mode (sends note off when touch is released)
      if (output) {
        touchwheel_output_set_release_callback(output, touchwheel_continuous_release_callback);
      }
      // Reset state when mode is initialized
      s_touchwheel_note_active = false;
      s_touchwheel_last_sent_cc = -1;  // Reset so first value is always sent
      break;
      
    case TOUCHWHEEL_MODE_SET_TEMPO:
      // Tempo mode: default to endless (only use odometer if explicitly set)
      s_touchwheel_tempo_bpm = tempo_get_bpm();  // Initialize to current BPM
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ODOMETER) {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "set_tempo (odometer)";
      } else {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "set_tempo (endless)";
      }
      output = touchwheel_output_callback_create(touchwheel_tempo_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_PITCH_BEND:
      // Pitch bend: true bipolar - position directly maps to value
      s_touchwheel_pitch_bend = 0;  // Start centered
      mode_proc = touchwheel_mode_create_bipolar();  // Position-based bipolar
      output = touchwheel_output_callback_create(touchwheel_pitch_bend_callback, NULL);
      if (output) {
        touchwheel_output_set_release_callback(output, touchwheel_pitch_bend_release_callback);
      }
      mode_desc = "pitch_bend (bipolar)";
      break;
      
    case TOUCHWHEEL_MODE_AFTERTOUCH:
      // Channel aftertouch: default to odometer, respects style
      s_touchwheel_aftertouch = 0;
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "aftertouch (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "aftertouch (odometer)";
      }
      output = touchwheel_output_callback_create(touchwheel_aftertouch_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_DOUBLE_CC:
      // Double CC (14-bit): default to endless for fine control
      s_touchwheel_14bit_value = 0;
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ODOMETER) {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "double_cc (odometer)";
      } else {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "double_cc (endless)";
      }
      output = touchwheel_output_callback_create(touchwheel_double_cc_callback, (void*)scene);
      break;
      
    case TOUCHWHEEL_MODE_VELOCITY:
      // Velocity mode: internal only - no MIDI output, just provides velocity value
      // Default to odometer for discrete velocity steps
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "velocity (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "velocity (odometer)";
      }
      // Velocity callback just updates the internal velocity value
      output = touchwheel_output_callback_create(touchwheel_velocity_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_LFO_RATE:
      // LFO rate mode: internal only - modulates LFO speed
      if (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) {
        mode_proc = touchwheel_mode_create_endless();
        mode_desc = "lfo_rate (endless)";
      } else {
        mode_proc = touchwheel_mode_create_odometer();
        mode_desc = "lfo_rate (odometer)";
      }
      // LFO rate callback just updates the internal LFO rate value
      output = touchwheel_output_callback_create(touchwheel_lfo_rate_callback, NULL);
      break;

    case TOUCHWHEEL_MODE_PADS:
    default:
      // No touchwheel instance needed for pads mode
      return;
  }
  
  if (mode_proc && output) {
    s_scene_touchwheel = touchwheel_create(mode_proc, output, 500);  // 500ms timeout
    if (s_scene_touchwheel) {
      touch_register_touchwheel_instance(s_scene_touchwheel);
      ESP_LOGD(TAG, "Created touchwheel instance for %s mode", mode_desc);
    } else {
      touchwheel_mode_destroy(mode_proc);
      touchwheel_output_destroy(output);
      ESP_LOGE(TAG, "Failed to create touchwheel instance");
    }
  } else {
    if (mode_proc) touchwheel_mode_destroy(mode_proc);
    if (output) touchwheel_output_destroy(output);
    ESP_LOGE(TAG, "Failed to create touchwheel mode or output");
  }
}

// Event handler for LFO rate source inputs
static void lfo_rate_source_event_handler(const event_t* event, void* context) {
  (void)context;
  
  switch (event->type) {
    case EVENT_EXPRESSION_VALUE:
      s_expression_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_CV_VALUE:
      s_cv_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_ALS:
      s_als_lfo_rate = event->data.sensor.value;
      break;
    case EVENT_SENSOR_PROXIMITY:
      s_proximity_lfo_rate = event->data.sensor.value;
      break;
    default:
      break;
  }
}

esp_err_t scene_init(void) {
  if (g_scene_manager.initialized) {
    ESP_LOGW(TAG, "Scene manager already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing scene manager with flash-based storage");
  
  // Load scene mode from NVS
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  // Load change mode from NVS
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  // Allocate scene cache from PSRAM to preserve internal RAM for SPI DMA
  if (g_scene_manager.cache == NULL) {
    g_scene_manager.cache = heap_caps_malloc(SCENE_CACHE_SIZE * sizeof(scene_cache_entry_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_scene_manager.cache == NULL) {
      ESP_LOGE(TAG, "Failed to allocate scene cache from PSRAM");
      return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Allocated scene cache (%d bytes) from PSRAM", 
      SCENE_CACHE_SIZE * (int)sizeof(scene_cache_entry_t));
  }
  
  // Initialize cache
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    g_scene_manager.cache[i].valid = false;
    g_scene_manager.cache[i].index = 0;
  }
  
  // Load or create scene manifest
  esp_err_t ret = scene_load_manifest();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to load manifest, creating default");
    // Create default manifest with one scene (use PSRAM for persistent data)
    g_scene_manager.manifest = malloc_prefer_psram(sizeof(scene_manifest_entry_t));
    if (!g_scene_manager.manifest) {
      ESP_LOGE(TAG, "Failed to allocate manifest");
      return ESP_ERR_NO_MEM;
    }
    g_scene_manager.num_scenes = 1;
    g_scene_manager.manifest[0].index = 0;
    strncpy(g_scene_manager.manifest[0].name, "Scene 1", sizeof(g_scene_manager.manifest[0].name));
    strncpy(g_scene_manager.manifest[0].filename, "scene_001.json", sizeof(g_scene_manager.manifest[0].filename));
  }
  
  // Load first scene into cache slot 0
  g_scene_manager.current_cache_idx = 0;
  g_scene_manager.current_scene_index = 0;
  
  // Load scene 0 directly into cache[0] (NOT using scene_load_from_flash which uses wrong slot)
  char filepath[128];
  get_scene_filename(0, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (f) {
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json_str = malloc(fsize + 1);
    if (json_str) {
      fread(json_str, 1, fsize, f);
      fclose(f);
      json_str[fsize] = '\0';
      
      cJSON* root = cJSON_Parse(json_str);
      free(json_str);
      
      if (root) {
        // Initialize with defaults first to ensure all fields have valid values,
        // even if JSON file is missing newer fields (e.g., velocity modes)
        scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
        ret = json_to_scene(root, &g_scene_manager.cache[0].scene);
        cJSON_Delete(root);
        
        if (ret == ESP_OK) {
          ESP_LOGI(TAG, "Loaded scene 0 from flash");
        } else {
          ESP_LOGW(TAG, "Failed to parse scene 0, using defaults");
          scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
        }
      } else {
        ESP_LOGW(TAG, "Failed to parse scene 0 JSON, using defaults");
        scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
      }
    } else {
      fclose(f);
      ESP_LOGW(TAG, "Failed to allocate memory for scene 0, using defaults");
      scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
    }
  } else {
    ESP_LOGW(TAG, "Scene 0 file not found, using defaults");
    scene_init_defaults(&g_scene_manager.cache[0].scene, 0);
  }
  
  g_scene_manager.cache[0].index = 0;
  g_scene_manager.cache[0].valid = true;

  // Subscribe to sensor events for LFO rate sources
  event_bus_subscribe(EVENT_EXPRESSION_VALUE, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_CV_VALUE, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_ALS, lfo_rate_source_event_handler, NULL);
  event_bus_subscribe(EVENT_SENSOR_PROXIMITY, lfo_rate_source_event_handler, NULL);

  g_scene_manager.initialized = true;

  const char* mode_str = (g_scene_manager.mode == SCENE_MODE_SINGLE) ? "Single" :
                         (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene manager initialized: mode=%s, total_scenes=%d", mode_str, g_scene_manager.num_scenes);
  
  // Initialize device current_program from scene's program_number
  // For PRESET_SYNC mode, initial scene is at position 0, so PC = 0 + min_preset
  scene_t* initial_scene = &g_scene_manager.cache[0].scene;
  uint8_t initial_program = (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC)
                            ? (uint8_t)device_config_get_min_preset()
                            : initial_scene->program_number;
  
  // In PRESET_SYNC mode, always send PC; otherwise respect per-scene flag
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || initial_scene->send_pc_on_load) {
    device_config_set_program(initial_program);
    ESP_LOGI(TAG, "Sent initial PC %d on channel %d", initial_program, device_config_get_channel());
  } else {
    ESP_LOGI(TAG, "Scene loaded but send_pc_on_load=false, PC not sent");
  }
  
  // Configure tempo settings for initial scene
  tempo_set_bpm(initial_scene->bpm);
  tempo_set_source(initial_scene->clock_source);
  tempo_set_note_divider(initial_scene->beat_divider);
  tempo_set_time_signature(initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set initial tempo: bpm=%d, source=%d, beat_divider=%d, time_sig=%d/%d", 
           initial_scene->bpm, initial_scene->clock_source, initial_scene->beat_divider,
           initial_scene->time_signature.numerator, initial_scene->time_signature.denominator);
  
  // Validate action timings against time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(initial_scene);
  
  // Execute on_load actions
  if (initial_scene->num_on_load_actions > 0) {
    ESP_LOGI(TAG, "Executing %d on_load action(s)", initial_scene->num_on_load_actions);
    for (int i = 0; i < initial_scene->num_on_load_actions; i++) {
      action_execute(&initial_scene->on_load[i], 127, true);
    }
  }
  
  // Apply LFO configurations from scene to LFO engine, then apply start modes
  lfo_apply_config(0, &initial_scene->lfo1_config);
  lfo_apply_config(1, &initial_scene->lfo2_config);
  lfo_apply_start_modes();
  
  // Setup touchwheel instance for non-buttons modes
  scene_setup_touchwheel_for_mode(initial_scene);
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = 0}
  };
  event_bus_post(&event);
  
  // Restore persisted scene if enabled and not already on scene 0
  if (config_get_persist_scene()) {
    uint8_t last_scene = config_get_last_scene();
    if (last_scene != 0) {
      // Verify the scene index exists in the manifest
      bool valid = false;
      for (int i = 0; i < g_scene_manager.num_scenes; i++) {
        if (g_scene_manager.manifest[i].index == last_scene) {
          valid = true;
          break;
        }
      }
      if (valid) {
        ESP_LOGI(TAG, "Restoring persisted scene %d", last_scene);
        scene_set_current(last_scene);
      } else {
        ESP_LOGW(TAG, "Persisted scene %d no longer exists, staying on scene 0", last_scene);
      }
    }
  }
  
  return ESP_OK;
}

esp_err_t scene_set_current(uint8_t scene_index) {
  if (!g_scene_manager.initialized) {
    ESP_LOGE(TAG, "Scene manager not initialized");
    return ESP_ERR_INVALID_STATE;
  }
  
  if (scene_index > MAX_SCENE_INDEX) {
    ESP_LOGE(TAG, "Invalid scene index %d", scene_index);
    return ESP_ERR_INVALID_ARG;
  }
  
  // Check if scene exists in manifest
  bool scene_exists = false;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) {
      scene_exists = true;
      break;
    }
  }
  
  if (!scene_exists) {
    ESP_LOGE(TAG, "Scene %d does not exist in manifest", scene_index);
    return ESP_ERR_NOT_FOUND;
  }
  
  // In pending mode, set pending instead of changing immediately
  if (g_scene_manager.change_mode == CHANGE_MODE_PENDING) {
    g_scene_manager.pending_scene_index = scene_index;
    g_scene_manager.has_pending_change = true;
    ESP_LOGI(TAG, "Pending scene change to %d (confirm or cancel)", scene_index + 1);
    return ESP_OK;
  }
  
  if (g_scene_manager.current_scene_index == scene_index) {
    return ESP_OK;  // Already on this scene
  }
  
  // Clean up any active notes before switching scenes
  touchwheel_cleanup_active_notes();
  
  // Clear any pending timed actions from the previous scene
  action_clear_pending();
  
  // Clear any active morphs from the previous scene
  action_clear_morphs();
  
  // Check if scene is already in cache
  int cache_idx = -1;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      cache_idx = i;
      break;
    }
  }
  
  // If not in cache, need to load it
  if (cache_idx == -1) {
    // Find least recently used cache slot (for now, just use round-robin)
    cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
    
    // Load new scene into cache
    esp_err_t ret = scene_load_from_flash(scene_index);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to load scene %d, using defaults", scene_index);
      scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
    }
    
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
  }
  
  g_scene_manager.current_cache_idx = cache_idx;
  g_scene_manager.current_scene_index = scene_index;
  
  // Invalidate cached device so it reloads for the new scene
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }
  
  scene_t* new_scene = &g_scene_manager.cache[cache_idx].scene;
  
  // Clear pending preset state - scene change supersedes preset changes
  if (device_config_has_pending_program()) {
    device_config_cancel_pending_program();
  }
  
  // Update device current_program and send PC based on mode
  // For PRESET_SYNC mode, use manifest position (ordinal) + indexBase
  uint8_t program;
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) {
    int position = 0;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == scene_index) {
        position = i;
        break;
      }
    }
    program = (uint8_t)(position + device_config_get_min_preset());
  } else {
    program = new_scene->program_number;
  }
  
  ESP_LOGI(TAG, "Switched to scene %d: %s", scene_index + 1, new_scene->name);
  
  // Configure expression jack mode for this scene
  expression_set_mode(new_scene->expression_mode);
  
  // Configure CV input mode for this scene
  input_set_mode(new_scene->cv_input_mode);
  
  // Configure tempo settings for this scene
  tempo_set_bpm(new_scene->bpm);
  tempo_set_source(new_scene->clock_source);
  tempo_set_note_divider(new_scene->beat_divider);
  tempo_set_time_signature(new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  ESP_LOGD(TAG, "Set tempo: bpm=%d, source=%d, beat_divider=%d, time_sig=%d/%d", 
           new_scene->bpm, new_scene->clock_source, new_scene->beat_divider,
           new_scene->time_signature.numerator, new_scene->time_signature.denominator);
  
  // Validate action timings against time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(new_scene);
  
  // Reset cut states on scene change (cut is a temporary runtime state)
  midi_out_reset_cut();
  
  // Apply LFO configurations (configures but does not start - no MIDI sent)
  lfo_apply_config(0, &new_scene->lfo1_config);
  lfo_apply_config(1, &new_scene->lfo2_config);
  
  // MIDI phase: PC send, on-load actions, LFO start
  // In programming mode, defer these until returning to performance mode
  if (!ui_is_in_programming_mode()) {
    if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || new_scene->send_pc_on_load) {
      device_config_set_program(program);
      ESP_LOGD(TAG, "Sent PC %d on channel %d", program, device_config_get_channel());
    }
    
    if (new_scene->num_on_load_actions > 0) {
      ESP_LOGD(TAG, "Executing %d on_load action(s)", new_scene->num_on_load_actions);
      for (int i = 0; i < new_scene->num_on_load_actions; i++) {
        action_execute(&new_scene->on_load[i], 127, true);
      }
    }
    
    lfo_apply_start_modes();
    s_needs_deferred_init = false;
  } else {
    ESP_LOGI(TAG, "Programming mode: deferring MIDI phase for scene %d", scene_index + 1);
    s_needs_deferred_init = true;
  }
  
  // Setup touchwheel instance for non-buttons modes
  scene_cleanup_touchwheel();
  scene_setup_touchwheel_for_mode(new_scene);
  
  // Persist current scene index if enabled
  if (config_get_persist_scene()) {
    config_set_last_scene(scene_index);
  }
  
  // Post event for scene change
  event_t event = {
    .type = EVENT_SCENE_CHANGED,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data = {.value_uint8 = scene_index}
  };
  event_bus_post(&event);
  
  return ESP_OK;
}

void scene_apply_deferred_init(void) {
  if (!s_needs_deferred_init) return;
  s_needs_deferred_init = false;
  
  scene_t* scene = scene_get_current();
  if (!scene) return;
  
  uint8_t scene_index = g_scene_manager.current_scene_index;
  ESP_LOGI(TAG, "Applying deferred MIDI init for scene %d: %s", scene_index + 1, scene->name);
  
  // Compute program number (same logic as scene_set_current)
  uint8_t program;
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC) {
    int position = 0;
    for (int i = 0; i < g_scene_manager.num_scenes; i++) {
      if (g_scene_manager.manifest[i].index == scene_index) {
        position = i;
        break;
      }
    }
    program = (uint8_t)(position + device_config_get_min_preset());
  } else {
    program = scene->program_number;
  }
  
  // Send PC
  if (g_scene_manager.mode == SCENE_MODE_PRESET_SYNC || scene->send_pc_on_load) {
    device_config_set_program(program);
    ESP_LOGD(TAG, "Deferred PC %d on channel %d", program, device_config_get_channel());
  }
  
  // Execute on-load actions
  if (scene->num_on_load_actions > 0) {
    ESP_LOGD(TAG, "Executing %d deferred on_load action(s)", scene->num_on_load_actions);
    for (int i = 0; i < scene->num_on_load_actions; i++) {
      action_execute(&scene->on_load[i], 127, true);
    }
  }
  
  // Start LFOs
  lfo_apply_start_modes();
}

uint8_t scene_get_current_index(void) {
  return g_scene_manager.current_scene_index;
}

scene_t* scene_get_current(void) {
  if (!g_scene_manager.initialized) return NULL;
  
  // Return current scene from cache
  int idx = g_scene_manager.current_cache_idx;
  if (idx >= 0 && idx < SCENE_CACHE_SIZE && g_scene_manager.cache[idx].valid) {
    return &g_scene_manager.cache[idx].scene;
  }
  
  return NULL;
}

esp_err_t scene_next(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to next in manifest (wrap around)
  int next_pos = (current_pos + 1) % g_scene_manager.num_scenes;
  return scene_set_current(g_scene_manager.manifest[next_pos].index);
}

esp_err_t scene_previous(void) {
  // Scene navigation disabled in single mode
  if (g_scene_manager.mode == SCENE_MODE_SINGLE) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  // Find current position in manifest
  int current_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == g_scene_manager.current_scene_index) {
      current_pos = i;
      break;
    }
  }
  
  if (current_pos == -1) return ESP_ERR_INVALID_STATE;
  
  // Move to previous in manifest (wrap around)
  int prev_pos = (current_pos == 0) ? g_scene_manager.num_scenes - 1 : current_pos - 1;
  return scene_set_current(g_scene_manager.manifest[prev_pos].index);
}

esp_err_t scene_set_name(uint8_t scene_index, const char* name) {
  if (scene_index > MAX_SCENE_INDEX || !name) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  strncpy(scene->name, name, sizeof(scene->name) - 1);
  scene->name[sizeof(scene->name) - 1] = '\0';
  scene_persist_if_programming();

  ESP_LOGI(TAG, "Scene %d renamed to: %s", scene_index + 1, name);
  return ESP_OK;
}

esp_err_t scene_set_device_id(uint8_t scene_index, const char* device_id) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  if (device_id && device_id[0] != '\0') {
    strncpy(scene->device_id, device_id, sizeof(scene->device_id) - 1);
    scene->device_id[sizeof(scene->device_id) - 1] = '\0';
    ESP_LOGI(TAG, "Scene %d device set to: %s", scene_index + 1, device_id);
  } else {
    scene->device_id[0] = '\0';
    ESP_LOGI(TAG, "Scene %d device cleared (using global)", scene_index + 1);
  }

  scene_persist_if_programming();

  // Invalidate cached device so it reloads on next access
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }

  return ESP_OK;
}

const char* scene_get_device_id(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return NULL;

  if (scene_index != g_scene_manager.current_scene_index) {
    // Can only get device_id for current scene
    return NULL;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  return scene->device_id;
}

esp_err_t scene_clear_device_id(uint8_t scene_index) {
  return scene_set_device_id(scene_index, NULL);
}

const char* scene_get_effective_device_slug(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return NULL;

  if (scene_index != g_scene_manager.current_scene_index) {
    return NULL;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return NULL;

  // If scene has a device_id, use it; otherwise use global
  if (scene->device_id[0] != '\0') {
    return scene->device_id;
  }

  // Fall back to global device_config
  return device_config_get_pedal_slug();
}

const struct device_def_t* scene_get_device(uint8_t scene_index) {
  const char* slug = scene_get_effective_device_slug(scene_index);
  if (!slug || slug[0] == '\0') {
    return NULL;  // No device configured
  }

  // Check if already cached
  if (s_cached_device && strcmp(s_cached_device_slug, slug) == 0) {
    return (const struct device_def_t*)s_cached_device;
  }

  // Free old cached device if any
  if (s_cached_device) {
    assets_free_device(s_cached_device);
    s_cached_device = NULL;
    s_cached_device_slug[0] = '\0';
  }

  // Load new device
  s_cached_device = assets_load_device(slug);
  if (s_cached_device) {
    strncpy(s_cached_device_slug, slug, sizeof(s_cached_device_slug) - 1);
    s_cached_device_slug[sizeof(s_cached_device_slug) - 1] = '\0';
    ESP_LOGI(TAG, "Loaded device: %s", slug);
  } else {
    ESP_LOGW(TAG, "Failed to load device: %s", slug);
  }

  return (const struct device_def_t*)s_cached_device;
}

esp_err_t scene_set_mode(scene_mode_t mode) {
  g_scene_manager.mode = mode;
  
  const char* mode_str = (mode == SCENE_MODE_SINGLE) ? "Single" :
                         (mode == SCENE_MODE_PRESET_SYNC) ? "Preset Sync" : "Advanced";
  ESP_LOGI(TAG, "Scene mode set to %s", mode_str);
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
}

scene_mode_t scene_get_mode(void) {
  return g_scene_manager.mode;
}

esp_err_t scene_set_change_mode(scene_change_mode_t mode) {
  g_scene_manager.change_mode = mode;
  
  ESP_LOGI(TAG, "Change mode set to %s", mode == CHANGE_MODE_IMMEDIATE ? "immediate" : "pending");
  
  uint8_t mode_val = (uint8_t)mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, mode_val);
}

scene_change_mode_t scene_get_change_mode(void) {
  return g_scene_manager.change_mode;
}

// Autosave mode removed - programming mode changes are always persisted immediately

esp_err_t scene_set_program_number(uint8_t scene_index, uint8_t program) {
  if (scene_index > MAX_SCENE_INDEX || program > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->program_number = program;
  scene_persist_if_programming();
  
  // Also update device's current_program and send PC
  device_config_set_program(program);
  
  ESP_LOGI(TAG, "Scene %d program number set to %d (PC sent)", scene_index + 1, program);
  return ESP_OK;
}

esp_err_t scene_set_send_pc_on_load(uint8_t scene_index, bool send_pc) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  // Get current scene if it matches
  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    scene->send_pc_on_load = send_pc;
    scene_persist_if_programming();
  } else {
    ESP_LOGW(TAG, "Can only modify current scene (load scene %d first)", scene_index);
    return ESP_ERR_INVALID_STATE;
  }
  ESP_LOGI(TAG, "Scene %d send PC on load: %s", scene_index + 1, send_pc ? "enabled" : "disabled");
  return ESP_OK;
}

esp_err_t scene_set_touchwheel_mode(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    // Clean up any active notes before changing mode
    touchwheel_cleanup_active_notes();
    
    scene->touchwheel_mode = mode;
    scene_persist_if_programming();

    // Re-setup touchwheel instance for new mode
    scene_cleanup_touchwheel();
    scene_setup_touchwheel_for_mode(scene);
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  
  const char* mode_str;
  switch (mode) {
    case TOUCHWHEEL_MODE_PADS: mode_str = "pads"; break;
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: mode_str = "program_change"; break;
    case TOUCHWHEEL_MODE_SET_TEMPO: mode_str = "set_tempo"; break;
    case TOUCHWHEEL_MODE_PITCH_BEND: mode_str = "pitch_bend"; break;
    case TOUCHWHEEL_MODE_AFTERTOUCH: mode_str = "aftertouch"; break;
    case TOUCHWHEEL_MODE_DOUBLE_CC: mode_str = "double_cc"; break;
    case TOUCHWHEEL_MODE_CONTINUOUS: mode_str = "continuous"; break;
    case TOUCHWHEEL_MODE_VELOCITY: mode_str = "velocity"; break;
    case TOUCHWHEEL_MODE_LFO_RATE: mode_str = "lfo_rate"; break;
    default: mode_str = "unknown"; break;
  }
  ESP_LOGI(TAG, "Scene %d touchwheel mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

// Runtime-only touchwheel mode change (no persistence)
// Used by touchwheel actions during performance - changes are temporary
esp_err_t scene_set_touchwheel_mode_runtime(uint8_t scene_index, touchwheel_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;

  scene_t* scene = scene_get_current();
  if (scene && g_scene_manager.current_scene_index == scene_index) {
    // Clean up any active notes before changing mode
    touchwheel_cleanup_active_notes();
    
    scene->touchwheel_mode = mode;
    // NOTE: No persistence - this is a runtime-only change

    // Re-setup touchwheel instance for new mode
    scene_cleanup_touchwheel();
    scene_setup_touchwheel_for_mode(scene);
  } else {
    ESP_LOGW(TAG, "Can only modify current scene");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGD(TAG, "Touchwheel mode set to %d (runtime, no persist)", mode);
  return ESP_OK;
}

esp_err_t scene_set_touchpad_cc(uint8_t scene_index, uint8_t pad_index, uint8_t cc_number, uint8_t value) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || cc_number > 127 || value > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  
  // Create simple CC action
  mapping->action = action_create_control(cc_number, value);
  
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d pad %d: CC%d value %d", 
    scene_index + 1, pad_index, cc_number, value);
  return ESP_OK;
}

esp_err_t scene_enable_touchpad(uint8_t scene_index, uint8_t pad_index, bool enabled) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->touchpads[pad_index].enabled = enabled;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d pad %d %s", scene_index + 1, pad_index, enabled ? "enabled" : "disabled");
  return ESP_OK;
}

touchpad_mapping_t* scene_get_touchpad_mapping(uint8_t scene_index, uint8_t pad_index) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS) return NULL;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return NULL;
  
  return &scene->touchpads[pad_index];
}

uint8_t scene_get_pending_index(void) {
  return g_scene_manager.pending_scene_index;
}

bool scene_has_pending_change(void) {
  return g_scene_manager.has_pending_change;
}

esp_err_t scene_confirm_change(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to confirm");
    return ESP_ERR_INVALID_STATE;
  }
  
  // Temporarily switch to immediate mode to perform the change
  scene_change_mode_t old_mode = g_scene_manager.change_mode;
  g_scene_manager.change_mode = CHANGE_MODE_IMMEDIATE;
  g_scene_manager.has_pending_change = false;
  
  esp_err_t ret = scene_set_current(g_scene_manager.pending_scene_index);
  
  // Restore pending mode
  g_scene_manager.change_mode = old_mode;
  
  return ret;
}

esp_err_t scene_cancel_pending(void) {
  if (!g_scene_manager.has_pending_change) {
    ESP_LOGW(TAG, "No pending scene change to cancel");
    return ESP_ERR_INVALID_STATE;
  }
  
  ESP_LOGI(TAG, "Cancelled pending scene change to %d", g_scene_manager.pending_scene_index + 1);
  g_scene_manager.has_pending_change = false;
  g_scene_manager.pending_scene_index = g_scene_manager.current_scene_index;
  
  return ESP_OK;
}


esp_err_t scene_save_config(void) {
  ESP_LOGI(TAG, "Saving scene configuration to NVS");
  
  uint8_t mode_val = (uint8_t)g_scene_manager.mode;
  esp_err_t ret = app_settings_save_u8(NVS_KEY_SCENE_MODE, mode_val);
  if (ret != ESP_OK) return ret;
  
  uint8_t change_val = (uint8_t)g_scene_manager.change_mode;
  return app_settings_save_u8(NVS_KEY_CHANGE_MODE, change_val);
}

esp_err_t scene_load_config(void) {
  uint8_t mode_val;
  if (app_settings_load_u8(NVS_KEY_SCENE_MODE, &mode_val) == ESP_OK) {
    g_scene_manager.mode = (scene_mode_t)mode_val;
  }
  
  uint8_t change_val;
  if (app_settings_load_u8(NVS_KEY_CHANGE_MODE, &change_val) == ESP_OK) {
    g_scene_manager.change_mode = (scene_change_mode_t)change_val;
  }
  
  return ESP_OK;
}

esp_err_t scene_process_touchpad(uint8_t pad_index, bool pressed) {
  if (!g_scene_manager.initialized || pad_index >= NUM_TOUCHPADS) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = scene_get_current();
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  if (!mapping->enabled) return ESP_OK;  // Pad is disabled
  
  // Handle touchwheel modes - pads 0-7 are routed to touchwheel instance
  // Touchwheel instance handles program change or continuous output
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_PADS && pad_index <= TOUCHWHEEL_END) {
    // Pad events are already routed to touchwheel instance via touch.c
    // Don't process as individual pad presses
    return ESP_OK;
  }
  
  // Execute action
  ESP_LOGD(TAG, "Pad %d %s: executing %s", pad_index, 
           pressed ? "pressed" : "released", action_type_to_string(mapping->action.type));
  
  // Notify touch component when hold actions start/end to suppress health check interventions
  if (action_requires_hold(mapping->action.type)) {
    touch_set_hold_active(pad_index, pressed);
  }
  
  return action_execute(&mapping->action, pressed ? 127 : 0, pressed);
}

esp_err_t scene_assign_touchpad_action(uint8_t scene_index, uint8_t pad_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || pad_index >= NUM_TOUCHPADS || !action) {
    return ESP_ERR_INVALID_ARG;
  }
  
  // Validate action against trigger type
  action_trigger_type_t trigger = (pad_index <= 7) ?
    ACTION_TRIGGER_TOUCHPAD_0_7 : ACTION_TRIGGER_TOUCHPAD_8_11;
  
  if (!action_is_valid_for_trigger(action->type, trigger)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to pad %d (invalid for this trigger)",
      action_type_to_string(action->type), pad_index);
    return ESP_ERR_NOT_SUPPORTED;
  }

  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;

  touchpad_mapping_t* mapping = &scene->touchpads[pad_index];
  mapping->action = *action;
  scene_persist_if_programming();

  ESP_LOGI(TAG, "Assigned action '%s' to pad %d", action_type_to_string(action->type), pad_index);
  return ESP_OK;
}

esp_err_t scene_assign_button_left(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to left button (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_left = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to left button: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_button_right(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to right button (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_right = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to right button: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_button_both(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against button trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_BUTTON)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to both buttons (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->button_both = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to both buttons: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_button_left(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_left : NULL;
}

action_t* scene_get_button_right(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_right : NULL;
}

action_t* scene_get_button_both(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->button_both : NULL;
}

esp_err_t scene_assign_bump(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against bump trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_BUMP)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to bump (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->bump = *action;
  scene_persist_if_programming();
  ESP_LOGI(TAG, "Assigned action to bump: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_bump(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->bump : NULL;
}

esp_err_t scene_add_on_load_action(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against on_load trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_ON_LOAD)) {
    ESP_LOGW(TAG, "Cannot add '%s' to on_load (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  if (scene->num_on_load_actions >= MAX_ON_LOAD_ACTIONS) {
    ESP_LOGW(TAG, "on_load already has %d actions (max)", MAX_ON_LOAD_ACTIONS);
    return ESP_ERR_NO_MEM;
  }
  
  scene->on_load[scene->num_on_load_actions++] = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Added on_load action: %s (now %d total)",
    action_type_to_string(action->type), scene->num_on_load_actions);
  return ESP_OK;
}

esp_err_t scene_clear_on_load_actions(uint8_t scene_index) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->num_on_load_actions = 0;
  memset(scene->on_load, 0, sizeof(scene->on_load));
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Cleared on_load actions");
  return ESP_OK;
}

uint8_t scene_get_num_on_load_actions(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->num_on_load_actions : 0;
}

action_t* scene_get_on_load_action(uint8_t scene_index, uint8_t action_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene || action_index >= scene->num_on_load_actions) return NULL;
  return &scene->on_load[action_index];
}

esp_err_t scene_set_expression_mode(uint8_t scene_index, expression_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  // State machine: Cannot change expression mode when in NOTE input mode (except to GATE)
  if (scene->cv_input_mode == INPUT_MODE_NOTE && mode != EXPRESSION_MODE_GATE) {
    ESP_LOGE(TAG, "Cannot change expression mode - locked by NOTE input mode");
    return ESP_ERR_INVALID_STATE;
  }
  
  scene->expression_mode = mode;
  
  // Auto-manage expression.enabled based on mode
  // Only EXPRESSION_MODE_PEDAL needs continuous value routing
  scene->expression.enabled = (mode == EXPRESSION_MODE_PEDAL);
  
  scene_persist_if_programming();
  
  // Update hardware configuration immediately if this is the current scene
  expression_set_mode(mode);
  
  const char* mode_str = (mode == EXPRESSION_MODE_NONE) ? "none" :
                         (mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" :
                         (mode == EXPRESSION_MODE_SWITCH) ? "switch" : "gate";
  ESP_LOGI(TAG, "Scene %d expression mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

expression_mode_t scene_get_expression_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_mode : EXPRESSION_MODE_PEDAL;
}

esp_err_t scene_assign_sustain(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sustain = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned sustain action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

esp_err_t scene_assign_sostenuto(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->sostenuto = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned sostenuto action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_sustain(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sustain : NULL;
}

action_t* scene_get_sostenuto(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->sostenuto : NULL;
}

esp_err_t scene_assign_expr_switch(uint8_t scene_index, const action_t* action) {
  if (scene_index > MAX_SCENE_INDEX || !action) return ESP_ERR_INVALID_ARG;
  
  // Validate action against expression switch trigger
  if (!action_is_valid_for_trigger(action->type, ACTION_TRIGGER_EXPR_SWITCH)) {
    ESP_LOGW(TAG, "Cannot assign '%s' to expr_switch (invalid for this trigger)",
      action_type_to_string(action->type));
    return ESP_ERR_NOT_SUPPORTED;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->expr_switch = *action;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Assigned expr_switch action: %s", action_type_to_string(action->type));
  return ESP_OK;
}

action_t* scene_get_expr_switch(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->expr_switch : NULL;
}

esp_err_t scene_set_cv_input_mode(uint8_t scene_index, input_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  input_mode_t old_mode = scene->cv_input_mode;
  scene->cv_input_mode = mode;
  
  // Auto-manage cv.enabled based on mode
  if (mode == INPUT_MODE_NONE) {
    scene->cv.enabled = false;
  } else if (mode == INPUT_MODE_CV) {
    scene->cv.enabled = true;
  }
  // INPUT_MODE_NOTE: enabled is managed by input_manager
  // INPUT_MODE_CLOCK_SYNC: CV is used for tempo, not continuous routing
  
  scene_persist_if_programming();
  
  // State machine: NOTE mode requires GATE expression mode
  if (mode == INPUT_MODE_NOTE) {
    scene->expression_mode = EXPRESSION_MODE_GATE;
    ESP_LOGI(TAG, "Expression mode automatically set to GATE for NOTE input mode");
    
    // Update hardware if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      expression_set_mode(EXPRESSION_MODE_GATE);
    }
  } else if (old_mode == INPUT_MODE_NOTE) {
    // Changing FROM NOTE mode - set expression to PEDAL mode
    scene->expression_mode = EXPRESSION_MODE_PEDAL;
    ESP_LOGI(TAG, "Expression mode set to PEDAL (leaving NOTE mode)");
    
    // Update hardware if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      expression_set_mode(EXPRESSION_MODE_PEDAL);
    }
  }
  
  const char* mode_str = (mode == INPUT_MODE_NONE) ? "none" :
                         (mode == INPUT_MODE_CV) ? "cv" :
                         (mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                         (mode == INPUT_MODE_AUDIO) ? "audio" : "note";
  ESP_LOGI(TAG, "Scene %d CV input mode set to %s", scene_index + 1, mode_str);
  return ESP_OK;
}

input_mode_t scene_get_cv_input_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_input_mode : INPUT_MODE_CV;
}

esp_err_t scene_set_bpm(uint8_t scene_index, uint16_t bpm) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (bpm < 20 || bpm > 300) return ESP_ERR_INVALID_ARG;
  
  // Scene BPM can only be modified in programming mode
  // Performance tempo changes should use tempo_set_bpm() directly
  if (!ui_is_in_programming_mode()) {
    ESP_LOGW(TAG, "Cannot modify scene BPM outside programming mode (use tempo bpm for live changes)");
    return ESP_ERR_INVALID_STATE;
  }
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->bpm = bpm;
  scene_persist_if_programming();
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_bpm(bpm);
  }
  
  ESP_LOGI(TAG, "Scene %d BPM set to %d", scene_index + 1, bpm);
  return ESP_OK;
}

uint16_t scene_get_bpm(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->bpm : 120;
}

esp_err_t scene_set_clock_source(uint8_t scene_index, tempo_clock_source_t source) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->clock_source = source;
  scene_persist_if_programming();
  
  // If setting to SYNC, automatically set cv_input_mode to CLOCK_SYNC for coherence
  if (source == CLOCK_SOURCE_SYNC) {
    input_mode_t old_input_mode = scene->cv_input_mode;
    scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    
    // If we were in NOTE mode, also reset expression mode to PEDAL
    if (old_input_mode == INPUT_MODE_NOTE) {
      scene->expression_mode = EXPRESSION_MODE_PEDAL;
      ESP_LOGI(TAG, "Expression mode set to PEDAL (leaving NOTE mode for clock sync)");
      
      if (scene_index == g_scene_manager.current_scene_index) {
        expression_set_mode(EXPRESSION_MODE_PEDAL);
      }
    }
    
    // Switch to clock sync mode if this is the current scene
    if (scene_index == g_scene_manager.current_scene_index) {
      input_set_mode(INPUT_MODE_CLOCK_SYNC);
      ESP_LOGI(TAG, "Switched to clock sync input mode");
    }
  }
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    ESP_LOGI(TAG, "Updating tempo to clock source %d (current scene)", source);
    tempo_set_source(source);
  } else {
    ESP_LOGI(TAG, "Not updating tempo (scene %u is not current scene %u)", 
             (unsigned)scene_index, (unsigned)g_scene_manager.current_scene_index);
  }
  
  const char* source_str = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                           (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Scene %d clock source set to %s", scene_index + 1, source_str);
  
  return ESP_OK;
}

tempo_clock_source_t scene_get_clock_source(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->clock_source : CLOCK_SOURCE_INTERNAL;
}

esp_err_t scene_set_beat_divider(uint8_t scene_index, tempo_note_divider_t divider) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->beat_divider = divider;
  scene_persist_if_programming();
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_note_divider(divider);
  }
  
  const char* div_str = (divider == DIVIDER_QUARTER) ? "Quarter" :
                        (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  ESP_LOGI(TAG, "Scene %d beat divider set to %s", scene_index + 1, div_str);
  
  return ESP_OK;
}

tempo_note_divider_t scene_get_beat_divider(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->beat_divider : DIVIDER_QUARTER;
}

esp_err_t scene_set_time_signature(uint8_t scene_index, uint8_t numerator, uint8_t denominator) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (numerator == 0 || numerator > 16 || denominator == 0 || denominator > 16) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->time_signature.numerator = numerator;
  scene->time_signature.denominator = denominator;
  scene_persist_if_programming();
  
  // Validate action timings against new time signature (remap invalid beats to beat 1)
  action_validate_scene_timings(scene);
  
  // Update tempo component immediately if this is the current scene
  if (scene_index == g_scene_manager.current_scene_index) {
    tempo_set_time_signature(numerator, denominator);
  }
  
  ESP_LOGI(TAG, "Scene %d time signature set to %d/%d", scene_index + 1, numerator, denominator);
  
  return ESP_OK;
}

time_signature_t scene_get_time_signature(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  time_signature_t default_sig = {4, 4};
  return scene ? scene->time_signature : default_sig;
}

esp_err_t scene_set_use_transport(uint8_t scene_index, bool use_transport) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->use_transport = use_transport;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d use_transport set to %s", scene_index + 1, 
           use_transport ? "true" : "false");
  
  return ESP_OK;
}

bool scene_get_use_transport(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->use_transport : false;
}

esp_err_t scene_set_send_clock(uint8_t scene_index, bool send_clock) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->send_clock = send_clock;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d send_clock set to %s", scene_index + 1,
    send_clock ? "true" : "false");
  
  return ESP_OK;
}

bool scene_get_send_clock(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->send_clock : true;  // Default to true if no scene
}

// Helper to get velocity mode name
static const char* velocity_mode_to_string(velocity_mode_t mode) {
  switch (mode) {
    case VELOCITY_MODE_FIXED: return "Fixed";
    case VELOCITY_MODE_GATE_VOLTAGE: return "Gate Voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "Touchwheel";
    default: return "Unknown";
  }
}

esp_err_t scene_set_cv_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->cv_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d CV velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_cv_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_cv_velocity(uint8_t scene_index, uint8_t velocity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (velocity < 1 || velocity > 127) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->cv_velocity = velocity;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d CV velocity set to %d", scene_index + 1, velocity);
  return ESP_OK;
}

uint8_t scene_get_cv_velocity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->cv_velocity : 100;
}

// Audio envelope follower configuration
esp_err_t scene_set_audio_range(uint8_t scene_index, cv_range_t range) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  // Only bipolar ranges are valid for audio
  if (range != CV_RANGE_BIPOLAR_5V && range != CV_RANGE_BIPOLAR_10V) {
    ESP_LOGW(TAG, "Audio mode requires bipolar range, defaulting to ±5V");
    range = CV_RANGE_BIPOLAR_5V;
  }
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.range = range;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio range set to %s", scene_index + 1,
    range == CV_RANGE_BIPOLAR_10V ? "±10V" : "±5V");
  return ESP_OK;
}

cv_range_t scene_get_audio_range(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.range : CV_RANGE_BIPOLAR_5V;
}

esp_err_t scene_set_audio_sensitivity(uint8_t scene_index, uint8_t sensitivity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.sensitivity = sensitivity;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio sensitivity set to %d", scene_index + 1, sensitivity);
  return ESP_OK;
}

uint8_t scene_get_audio_sensitivity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.sensitivity : 128;
}

esp_err_t scene_set_audio_attack(uint8_t scene_index, uint16_t attack_ms) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (attack_ms < 5) attack_ms = 5;
  if (attack_ms > 100) attack_ms = 100;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.attack_ms = attack_ms;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio attack set to %ums", scene_index + 1, attack_ms);
  return ESP_OK;
}

uint16_t scene_get_audio_attack(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.attack_ms : 10;
}

esp_err_t scene_set_audio_release(uint8_t scene_index, uint16_t release_ms) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (release_ms < 50) release_ms = 50;
  if (release_ms > 2000) release_ms = 2000;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.release_ms = release_ms;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio release set to %ums", scene_index + 1, release_ms);
  return ESP_OK;
}

uint16_t scene_get_audio_release(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.release_ms : 200;
}

esp_err_t scene_set_audio_threshold(uint8_t scene_index, uint8_t threshold) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  if (threshold > 127) threshold = 127;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.threshold = threshold;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio threshold set to %d", scene_index + 1, threshold);
  return ESP_OK;
}

uint8_t scene_get_audio_threshold(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.threshold : 5;
}

esp_err_t scene_set_audio_polarity(uint8_t scene_index, audio_polarity_t polarity) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  scene->audio_config.polarity = polarity;
  scene_persist_if_programming();
  ESP_LOGD(TAG, "Scene %d audio polarity set to %s", scene_index + 1,
    polarity == AUDIO_POLARITY_ATTRACT ? "Attract" : "Repel");
  return ESP_OK;
}

audio_polarity_t scene_get_audio_polarity(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->audio_config.polarity : AUDIO_POLARITY_ATTRACT;
}

audio_config_t* scene_get_audio_config(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? &scene->audio_config : NULL;
}

esp_err_t scene_set_expression_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->expression_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d expression velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_expression_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->expression_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_proximity_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->proximity_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d proximity velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_proximity_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->proximity_velocity_mode : VELOCITY_MODE_FIXED;
}

esp_err_t scene_set_als_velocity_mode(uint8_t scene_index, velocity_mode_t mode) {
  if (scene_index > MAX_SCENE_INDEX) return ESP_ERR_INVALID_ARG;
  
  scene_t* scene = get_scene_for_modification(scene_index);
  if (!scene) return ESP_ERR_INVALID_STATE;
  
  scene->als_velocity_mode = mode;
  scene_persist_if_programming();
  
  ESP_LOGI(TAG, "Scene %d ALS velocity mode set to %s", scene_index + 1, velocity_mode_to_string(mode));
  return ESP_OK;
}

velocity_mode_t scene_get_als_velocity_mode(uint8_t scene_index) {
  scene_t* scene = get_scene_for_modification(scene_index);
  return scene ? scene->als_velocity_mode : VELOCITY_MODE_FIXED;
}

uint8_t scene_get_touchwheel_velocity(void) {
  scene_t* scene = scene_get_current();
  if (!scene || scene->touchwheel_mode != TOUCHWHEEL_MODE_VELOCITY) {
    return 100;  // Default velocity when not in velocity mode
  }
  return s_touchwheel_velocity;
}

uint8_t scene_get_touchwheel_lfo_rate(void) {
  scene_t* scene = scene_get_current();
  if (!scene || scene->touchwheel_mode != TOUCHWHEEL_MODE_LFO_RATE) {
    return 64;  // Default center value when not in LFO rate mode
  }
  return s_touchwheel_lfo_rate;
}

uint8_t scene_get_expression_lfo_rate(void) {
  return s_expression_lfo_rate;
}

uint8_t scene_get_cv_lfo_rate(void) {
  return s_cv_lfo_rate;
}

uint8_t scene_get_als_lfo_rate(void) {
  return s_als_lfo_rate;
}

uint8_t scene_get_proximity_lfo_rate(void) {
  return s_proximity_lfo_rate;
}

// Helper to get scene filename
static void get_scene_filename(uint8_t scene_index, char* buffer, size_t buffer_size) {
  snprintf(buffer, buffer_size, "%s/scene_%03d.json", SCENES_BASE_PATH, scene_index + 1);
}

// Action type name lookup table (for JSON serialization)
// Note: ACTION_NONE is NULL - we skip serializing empty actions
static const char* action_type_json_names[] = {
  [ACTION_NONE] = NULL,  // Don't serialize empty actions
  [ACTION_PRESET_INC] = "preset_inc",
  [ACTION_PRESET_DEC] = "preset_dec",
  [ACTION_PRESET] = "preset",
  [ACTION_PRESET_HOLD] = "preset_hold",
  [ACTION_PRESET_CYCLE] = "preset_cycle",
  [ACTION_SCENE_INC] = "scene_inc",
  [ACTION_SCENE_DEC] = "scene_dec",
  [ACTION_SCENE] = "scene",
  [ACTION_PLAY] = "play",
  [ACTION_STOP] = "stop",
  [ACTION_PAUSE] = "pause",
  [ACTION_RECORD] = "record",
  [ACTION_TAP] = "tap",
  [ACTION_TAP_TEMPO] = "tap_tempo",
  [ACTION_SET_TEMPO] = "set_tempo",
  [ACTION_TEMPO_INC] = "tempo_inc",
  [ACTION_TEMPO_DEC] = "tempo_dec",
  [ACTION_TEMPO_HOLD] = "tempo_hold",
  [ACTION_TEMPO_CYCLE] = "tempo_cycle",
  [ACTION_CONTROL_CHANGE] = "control_change",
  [ACTION_CONTROL_HOLD] = "control_hold",
  [ACTION_CONTROL_CYCLE] = "control_cycle",
  [ACTION_NOTE] = "note",
  [ACTION_RANDOMIZE] = "randomize",
  [ACTION_CONFIRM_PENDING] = "confirm_pending",
  [ACTION_RESET] = "reset",
  [ACTION_SUSTAIN] = "sustain",
  [ACTION_SOSTENUTO] = "sostenuto",
  [ACTION_TOUCHWHEEL_HOLD] = "touchwheel_hold",
  [ACTION_TOUCHWHEEL_CYCLE] = "touchwheel_cycle",
  [ACTION_LFO_START] = "lfo_start",
  [ACTION_LFO_STOP] = "lfo_stop",
  [ACTION_LFO_TOGGLE] = "lfo_toggle",
  [ACTION_LFO_SHAPE] = "lfo_shape",
  [ACTION_CLOCK_TOGGLE] = "clock_toggle",
  [ACTION_CLOCK_HOLD] = "clock_hold",
  [ACTION_CLOCK_BURST] = "clock_burst",
  [ACTION_CUT_TOGGLE] = "cut_toggle",
  [ACTION_CUT_HOLD] = "cut_hold"
};

// Helper to convert action type string to enum
static action_type_t action_type_from_string(const char* name) {
  if (!name) return ACTION_NONE;
  
  // Check current names
  for (int i = 0; i < ACTION_MAX; i++) {
    if (action_type_json_names[i] && strcmp(name, action_type_json_names[i]) == 0) {
      return (action_type_t)i;
    }
  }
  
  // Backward compatibility for old action names
  if (strcmp(name, "transport_play") == 0) return ACTION_PLAY;
  if (strcmp(name, "transport_stop") == 0) return ACTION_STOP;
  if (strcmp(name, "transport_pause") == 0) return ACTION_PAUSE;
  if (strcmp(name, "transport_record") == 0) return ACTION_RECORD;
  if (strcmp(name, "all_notes_off") == 0) return ACTION_RESET;
  if (strcmp(name, "all_sound_off") == 0) return ACTION_RESET;
  if (strcmp(name, "send_reset") == 0) return ACTION_RESET;
  // Old preset/program names
  if (strcmp(name, "program_next") == 0) return ACTION_PRESET_INC;
  if (strcmp(name, "program_prev") == 0) return ACTION_PRESET_DEC;
  if (strcmp(name, "pc") == 0) return ACTION_PRESET;
  // Old scene names
  if (strcmp(name, "scene_next") == 0) return ACTION_SCENE_INC;
  if (strcmp(name, "scene_prev") == 0) return ACTION_SCENE_DEC;
  if (strcmp(name, "scene_set") == 0) return ACTION_SCENE;
  // Old CC/control names (backward compatibility)
  if (strcmp(name, "control") == 0) return ACTION_CONTROL_CHANGE;
  if (strcmp(name, "send_cc") == 0) return ACTION_CONTROL_CHANGE;
  if (strcmp(name, "send_cc_hold") == 0) return ACTION_CONTROL_HOLD;
  if (strcmp(name, "send_cc_cycle") == 0) return ACTION_CONTROL_CYCLE;
  // Old note names (both map to the new hold-style ACTION_NOTE)
  if (strcmp(name, "send_note_on") == 0) return ACTION_NOTE;
  if (strcmp(name, "send_note_off") == 0) return ACTION_NOTE;
  // Old randomize name
  if (strcmp(name, "randomize_cc") == 0) return ACTION_RANDOMIZE;
  // Old touchwheel names (tw_mode removed, map to NONE for backward compat)
  if (strcmp(name, "tw_mode") == 0) return ACTION_NONE;
  if (strcmp(name, "touchwheel") == 0) return ACTION_NONE;  // Old JSON name
  if (strcmp(name, "tw_mode_hold") == 0) return ACTION_TOUCHWHEEL_HOLD;
  if (strcmp(name, "tw_mode_cycle") == 0) return ACTION_TOUCHWHEEL_CYCLE;
  
  return ACTION_NONE;
}

// Serialize/deserialize actions
// Returns NULL for ACTION_NONE (empty actions should be skipped)
static cJSON* action_to_json(const action_t* action) {
  // Don't serialize empty actions
  if (!action || action->type == ACTION_NONE) {
    return NULL;
  }
  
  cJSON* obj = cJSON_CreateObject();
  
  // Use string name instead of integer
  if (action->type < ACTION_MAX && action_type_json_names[action->type]) {
    cJSON_AddStringToObject(obj, "type", action_type_json_names[action->type]);
  } else {
    // Unknown action type - skip it
    cJSON_Delete(obj);
    return NULL;
  }
  
  if (action->type == ACTION_CONTROL_CHANGE || action->type == ACTION_CONTROL_HOLD) {
    uint8_t num_ccs = action->params.control.num_ccs;
    if (num_ccs == 0) num_ccs = 1;  // Backward compat
    
    if (num_ccs == 1) {
      // Single CC: use simple format for backward compatibility
      cJSON_AddNumberToObject(obj, "cc", action->params.control.cc_numbers[0]);
      cJSON_AddNumberToObject(obj, "value", action->params.control.values[0]);
      if (action->type == ACTION_CONTROL_HOLD) {
        cJSON_AddNumberToObject(obj, "value2", action->params.control.values2[0]);
      }
    } else {
      // Multi-CC: use array format
      cJSON* cc_arr = cJSON_CreateArray();
      cJSON* val_arr = cJSON_CreateArray();
      cJSON* val2_arr = (action->type == ACTION_CONTROL_HOLD) ? cJSON_CreateArray() : NULL;
      for (int i = 0; i < num_ccs && i < 4; i++) {
        cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.control.cc_numbers[i]));
        cJSON_AddItemToArray(val_arr, cJSON_CreateNumber(action->params.control.values[i]));
        if (val2_arr) {
          cJSON_AddItemToArray(val2_arr, cJSON_CreateNumber(action->params.control.values2[i]));
        }
      }
      cJSON_AddItemToObject(obj, "cc", cc_arr);
      cJSON_AddItemToObject(obj, "value", val_arr);
      if (val2_arr) cJSON_AddItemToObject(obj, "value2", val2_arr);
    }
  } else if (action->type == ACTION_CONTROL_CYCLE) {
    uint8_t num_ccs = action->params.control.num_ccs;
    if (num_ccs == 0) num_ccs = 1;
    uint8_t num_steps = action->params.control.num_cycle_steps;
    
    if (num_ccs == 1) {
      // Single CC cycle: simple format
      cJSON_AddNumberToObject(obj, "cc", action->params.control.cc_numbers[0]);
      cJSON* vals = cJSON_CreateArray();
      for (int i = 0; i < num_steps && i < 8; i++) {
        cJSON_AddItemToArray(vals, cJSON_CreateNumber(action->params.control.cycle_values[0][i]));
      }
      cJSON_AddItemToObject(obj, "values", vals);
    } else {
      // Multi-CC cycle: array of arrays
      cJSON* cc_arr = cJSON_CreateArray();
      cJSON* vals_arr = cJSON_CreateArray();
      for (int i = 0; i < num_ccs && i < 4; i++) {
        cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.control.cc_numbers[i]));
        cJSON* steps = cJSON_CreateArray();
        for (int j = 0; j < num_steps && j < 8; j++) {
          cJSON_AddItemToArray(steps, cJSON_CreateNumber(action->params.control.cycle_values[i][j]));
        }
        cJSON_AddItemToArray(vals_arr, steps);
      }
      cJSON_AddItemToObject(obj, "cc", cc_arr);
      cJSON_AddItemToObject(obj, "values", vals_arr);
    }
  } else if (action->type == ACTION_RANDOMIZE) {
    // Randomize uses a different struct
    cJSON* cc_arr = cJSON_CreateArray();
    for (int i = 0; i < action->params.randomize.num_ccs && i < 8; i++) {
      cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(action->params.randomize.cc_numbers[i]));
    }
    cJSON_AddItemToObject(obj, "cc", cc_arr);
  } else if (action->type == ACTION_NOTE) {
    cJSON_AddNumberToObject(obj, "note", action->params.note.note);
    cJSON_AddNumberToObject(obj, "velocity", action->params.note.velocity);
  } else if (action->type == ACTION_PRESET || action->type == ACTION_SCENE) {
    cJSON_AddNumberToObject(obj, "number", action->params.target.number);
  } else if (action->type == ACTION_PRESET_HOLD) {
    cJSON_AddNumberToObject(obj, "press_preset", action->params.preset_cycle.press_preset);
    cJSON_AddNumberToObject(obj, "release_preset", action->params.preset_cycle.release_preset);
  } else if (action->type == ACTION_PRESET_CYCLE) {
    uint8_t num_presets = action->params.preset_cycle.num_presets;
    cJSON_AddNumberToObject(obj, "num_presets", num_presets);
    cJSON* presets = cJSON_CreateArray();
    for (int i = 0; i < num_presets && i < 8; i++) {
      cJSON_AddItemToArray(presets, cJSON_CreateNumber(action->params.preset_cycle.cycle_presets[i]));
    }
    cJSON_AddItemToObject(obj, "presets", presets);
  } else if (action->type == ACTION_SET_TEMPO) {
    cJSON_AddNumberToObject(obj, "bpm", action->params.tempo.bpm);
  } else if (action->type == ACTION_TEMPO_HOLD) {
    cJSON_AddNumberToObject(obj, "press_bpm", action->params.tempo.press_bpm);
    cJSON_AddNumberToObject(obj, "release_bpm", action->params.tempo.release_bpm);
  } else if (action->type == ACTION_TEMPO_CYCLE) {
    uint8_t num_tempos = action->params.tempo.num_tempos;
    cJSON_AddNumberToObject(obj, "num_tempos", num_tempos);
    cJSON* tempos = cJSON_CreateArray();
    for (int i = 0; i < num_tempos && i < 8; i++) {
      cJSON_AddItemToArray(tempos, cJSON_CreateNumber(action->params.tempo.cycle_tempos[i]));
    }
    cJSON_AddItemToObject(obj, "tempos", tempos);
  } else if (action->type == ACTION_TOUCHWHEEL_HOLD) {
    cJSON_AddNumberToObject(obj, "mode", action->params.tw_mode.mode);
    cJSON_AddNumberToObject(obj, "mode2", action->params.tw_mode.mode2);
  } else if (action->type == ACTION_TOUCHWHEEL_CYCLE) {
    cJSON_AddNumberToObject(obj, "num_modes", action->params.tw_mode.num_modes);
    cJSON* modes = cJSON_CreateArray();
    for (int i = 0; i < action->params.tw_mode.num_modes; i++) {
      cJSON_AddItemToArray(modes, cJSON_CreateNumber(action->params.tw_mode.modes[i]));
    }
    cJSON_AddItemToObject(obj, "modes", modes);
  } else if (action->type == ACTION_LFO_START || action->type == ACTION_LFO_STOP ||
             action->type == ACTION_LFO_TOGGLE) {
    cJSON_AddNumberToObject(obj, "slot", action->params.lfo.slot);
  } else if (action->type == ACTION_LFO_SHAPE) {
    cJSON_AddNumberToObject(obj, "slot", action->params.lfo.slot);
    cJSON_AddNumberToObject(obj, "num_shapes", action->params.lfo.num_shapes);
    cJSON* shapes = cJSON_CreateArray();
    for (int i = 0; i < action->params.lfo.num_shapes && i < 8; i++) {
      cJSON_AddItemToArray(shapes, cJSON_CreateNumber(action->params.lfo.shapes[i]));
    }
    cJSON_AddItemToObject(obj, "shapes", shapes);
  } else if (action->type == ACTION_CLOCK_TOGGLE || action->type == ACTION_CLOCK_HOLD) {
    cJSON_AddBoolToObject(obj, "start_enabled", action->params.clock.start_enabled);
  } else if (action->type == ACTION_CLOCK_BURST) {
    cJSON_AddNumberToObject(obj, "speed_percent", action->params.clock_burst.speed_percent);
  } else if (action->type == ACTION_CUT_TOGGLE || action->type == ACTION_CUT_HOLD) {
    const char* mode_str = (action->params.cut.cut_mode == 0) ? "local" :
                           (action->params.cut.cut_mode == 1) ? "passthrough" : "both";
    cJSON_AddStringToObject(obj, "cut_mode", mode_str);
  } else if (action->type == ACTION_CONFIRM_PENDING) {
    // Only serialize if not default (preset = 0)
    if (action->params.confirm.target == CONFIRM_TARGET_SCENE) {
      cJSON_AddStringToObject(obj, "confirm_target", "scene");
    }
  }
  
  // Serialize timing (only if not immediate default)
  if (action->timing != ACTION_TIMING_IMMEDIATE) {
    const char* timing_str = action_timing_to_string(action->timing, action->timing_beat);
    cJSON_AddStringToObject(obj, "timing", timing_str);
  }
  
  // Serialize repeat settings (only if enabled)
  if (action->repeat_enabled) {
    cJSON_AddBoolToObject(obj, "repeat", true);
    cJSON_AddStringToObject(obj, "repeat_division",
      action_repeat_division_to_string(action->repeat_division));
    // Only serialize probability if not default (100%)
    if (action->probability > 0 && action->probability < 100) {
      cJSON_AddNumberToObject(obj, "probability", action->probability);
    }
    // Only serialize pattern if enabled (length >= 2)
    if (action->pattern_length >= 2) {
      cJSON_AddNumberToObject(obj, "pattern_length", action->pattern_length);
      cJSON_AddNumberToObject(obj, "pattern_mask", action->pattern_mask);
    }
  }
  
  // Serialize morph settings (only if enabled, for CONTROL_HOLD, CONTROL_CYCLE, RANDOMIZE)
  if (action->morph_enabled && action_supports_morph(action->type)) {
    cJSON_AddBoolToObject(obj, "morph", true);
    // Only serialize non-default values
    if (action->morph_steps_mode != MORPH_STEPS_AUTO) {
      cJSON_AddStringToObject(obj, "morph_steps", 
        morph_steps_mode_to_string(action->morph_steps_mode));
    }
    if (action->morph_steps_mode == MORPH_STEPS_MANUAL && action->morph_manual_steps != 32) {
      cJSON_AddNumberToObject(obj, "morph_manual_steps", action->morph_manual_steps);
    }
    if (action->morph_timing_mode != MORPH_TIMING_FEEL) {
      cJSON_AddStringToObject(obj, "morph_timing",
        morph_timing_mode_to_string(action->morph_timing_mode));
    }
    if (action->morph_timing_mode == MORPH_TIMING_FEEL && 
        action->morph_feel != MORPH_FEEL_MEDIUM) {
      cJSON_AddStringToObject(obj, "morph_feel", morph_feel_to_string(action->morph_feel));
    }
    if (action->morph_timing_mode != MORPH_TIMING_FEEL && 
        action->morph_division != MORPH_DIV_BAR) {
      cJSON_AddStringToObject(obj, "morph_division", 
        morph_division_to_string(action->morph_division));
    }
  }

  return obj;
}

static action_t json_to_action(cJSON* obj) {
  action_t action = {0};
  cJSON* type = cJSON_GetObjectItem(obj, "type");
  
  if (type) {
    if (cJSON_IsString(type)) {
      // New format: string name
      action.type = action_type_from_string(type->valuestring);
      ESP_LOGD(TAG, "Loaded action: %s -> %d", type->valuestring, action.type);
    } else if (cJSON_IsNumber(type)) {
      // Legacy format: integer (for backward compatibility)
      action.type = (action_type_t)type->valueint;
      ESP_LOGW(TAG, "Loading action with legacy integer type %d (use string names)", type->valueint);
    } else {
      ESP_LOGE(TAG, "Action type is neither string nor number!");
    }
  } else {
    ESP_LOGE(TAG, "Action missing 'type' field!");
  }
  
  // Parse CC actions (supports both single and multi-CC formats)
  cJSON* cc = cJSON_GetObjectItem(obj, "cc");
  cJSON* value = cJSON_GetObjectItem(obj, "value");
  cJSON* value2 = cJSON_GetObjectItem(obj, "value2");
  cJSON* values = cJSON_GetObjectItem(obj, "values");
  
  if (cc) {
    if (cJSON_IsArray(cc)) {
      // Multi-CC format: cc is array
      int num_ccs = cJSON_GetArraySize(cc);
      if (num_ccs > 4) num_ccs = 4;
      action.params.control.num_ccs = num_ccs;
      for (int i = 0; i < num_ccs; i++) {
        cJSON* item = cJSON_GetArrayItem(cc, i);
        if (item) action.params.control.cc_numbers[i] = item->valueint;
      }
      // Parse value array
      if (value && cJSON_IsArray(value)) {
        for (int i = 0; i < num_ccs; i++) {
          cJSON* item = cJSON_GetArrayItem(value, i);
          if (item) action.params.control.values[i] = item->valueint;
        }
      }
      // Parse value2 array (for hold)
      if (value2 && cJSON_IsArray(value2)) {
        for (int i = 0; i < num_ccs; i++) {
          cJSON* item = cJSON_GetArrayItem(value2, i);
          if (item) action.params.control.values2[i] = item->valueint;
        }
      }
      // Parse cycle values (array of arrays)
      if (values && cJSON_IsArray(values)) {
        cJSON* first = cJSON_GetArrayItem(values, 0);
        if (first && cJSON_IsArray(first)) {
          // Multi-CC cycle: values is array of arrays
          int num_steps = cJSON_GetArraySize(first);
          if (num_steps > 8) num_steps = 8;
          action.params.control.num_cycle_steps = num_steps;
          for (int i = 0; i < num_ccs; i++) {
            cJSON* steps = cJSON_GetArrayItem(values, i);
            if (steps && cJSON_IsArray(steps)) {
              for (int j = 0; j < num_steps; j++) {
                cJSON* item = cJSON_GetArrayItem(steps, j);
                if (item) action.params.control.cycle_values[i][j] = item->valueint;
              }
            }
          }
        }
      }
    } else {
      // Single CC format (backward compatible)
      action.params.control.num_ccs = 1;
      action.params.control.cc_numbers[0] = cc->valueint;
      if (value && cJSON_IsNumber(value)) {
        action.params.control.values[0] = value->valueint;
      }
      if (value2 && cJSON_IsNumber(value2)) {
        action.params.control.values2[0] = value2->valueint;
      }
      // Parse single CC cycle values
      if (values && cJSON_IsArray(values)) {
        int num_steps = cJSON_GetArraySize(values);
        if (num_steps > 8) num_steps = 8;
        action.params.control.num_cycle_steps = num_steps;
        for (int i = 0; i < num_steps; i++) {
          cJSON* item = cJSON_GetArrayItem(values, i);
          if (item) action.params.control.cycle_values[0][i] = item->valueint;
        }
      }
    }
  }
  
  // Parse randomize CC (uses different struct, always array format)
  if (action.type == ACTION_RANDOMIZE && cc && cJSON_IsArray(cc)) {
    int num_ccs = cJSON_GetArraySize(cc);
    if (num_ccs > 8) num_ccs = 8;
    action.params.randomize.num_ccs = num_ccs;
    for (int i = 0; i < num_ccs; i++) {
      cJSON* item = cJSON_GetArrayItem(cc, i);
      if (item) action.params.randomize.cc_numbers[i] = item->valueint;
    }
  }
  
  // Parse note actions
  cJSON* note = cJSON_GetObjectItem(obj, "note");
  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  if (note) action.params.note.note = note->valueint;
  if (velocity) action.params.note.velocity = velocity->valueint;
  
  // Parse target/scene/program actions
  cJSON* number = cJSON_GetObjectItem(obj, "number");
  if (number) action.params.target.number = number->valueint;
  
  // Parse preset hold/cycle actions
  if (action.type == ACTION_PRESET_HOLD) {
    cJSON* press = cJSON_GetObjectItem(obj, "press_preset");
    cJSON* release = cJSON_GetObjectItem(obj, "release_preset");
    if (press) action.params.preset_cycle.press_preset = press->valueint;
    if (release) action.params.preset_cycle.release_preset = release->valueint;
  }
  if (action.type == ACTION_PRESET_CYCLE) {
    cJSON* num_presets = cJSON_GetObjectItem(obj, "num_presets");
    cJSON* presets = cJSON_GetObjectItem(obj, "presets");
    if (num_presets) {
      action.params.preset_cycle.num_presets = num_presets->valueint;
    }
    if (presets && cJSON_IsArray(presets)) {
      int count = cJSON_GetArraySize(presets);
      if (count > 8) count = 8;
      for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(presets, i);
        if (item) action.params.preset_cycle.cycle_presets[i] = item->valueint;
      }
    }
  }
  
  // Parse tempo actions
  cJSON* bpm = cJSON_GetObjectItem(obj, "bpm");
  if (bpm) action.params.tempo.bpm = bpm->valueint;
  
  // Parse tempo hold/cycle actions
  if (action.type == ACTION_TEMPO_HOLD) {
    cJSON* press_bpm = cJSON_GetObjectItem(obj, "press_bpm");
    cJSON* release_bpm = cJSON_GetObjectItem(obj, "release_bpm");
    if (press_bpm) action.params.tempo.press_bpm = press_bpm->valueint;
    if (release_bpm) action.params.tempo.release_bpm = release_bpm->valueint;
  }
  if (action.type == ACTION_TEMPO_CYCLE) {
    cJSON* num_tempos = cJSON_GetObjectItem(obj, "num_tempos");
    cJSON* tempos = cJSON_GetObjectItem(obj, "tempos");
    if (num_tempos) {
      action.params.tempo.num_tempos = num_tempos->valueint;
    }
    if (tempos && cJSON_IsArray(tempos)) {
      int count = cJSON_GetArraySize(tempos);
      if (count > 8) count = 8;
      for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(tempos, i);
        if (item) action.params.tempo.cycle_tempos[i] = item->valueint;
      }
    }
  }
  
  // Parse touchwheel mode actions
  cJSON* mode = cJSON_GetObjectItem(obj, "mode");
  cJSON* mode2 = cJSON_GetObjectItem(obj, "mode2");
  cJSON* num_modes = cJSON_GetObjectItem(obj, "num_modes");
  cJSON* modes = cJSON_GetObjectItem(obj, "modes");
  if (mode) action.params.tw_mode.mode = mode->valueint;
  if (mode2) action.params.tw_mode.mode2 = mode2->valueint;
  if (num_modes) action.params.tw_mode.num_modes = num_modes->valueint;
  if (modes && cJSON_IsArray(modes)) {
    int count = cJSON_GetArraySize(modes);
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
      cJSON* item = cJSON_GetArrayItem(modes, i);
      if (item) action.params.tw_mode.modes[i] = item->valueint;
    }
  }
  
  // Parse LFO actions
  if (action.type == ACTION_LFO_START || action.type == ACTION_LFO_STOP ||
      action.type == ACTION_LFO_TOGGLE || action.type == ACTION_LFO_SHAPE) {
    cJSON* slot = cJSON_GetObjectItem(obj, "slot");
    if (slot) action.params.lfo.slot = slot->valueint;
    
    if (action.type == ACTION_LFO_SHAPE) {
      cJSON* num_shapes = cJSON_GetObjectItem(obj, "num_shapes");
      cJSON* shapes = cJSON_GetObjectItem(obj, "shapes");
      if (num_shapes) {
        int ns = num_shapes->valueint;
        // Clamp to valid range: 2-8 (need at least 2 shapes to cycle)
        if (ns < 2) ns = 2;
        if (ns > 8) ns = 8;
        action.params.lfo.num_shapes = (uint8_t)ns;
      }
      if (shapes && cJSON_IsArray(shapes)) {
        int count = cJSON_GetArraySize(shapes);
        if (count > 8) count = 8;
        for (int i = 0; i < count; i++) {
          cJSON* item = cJSON_GetArrayItem(shapes, i);
          if (item) action.params.lfo.shapes[i] = (uint8_t)item->valueint;
        }
        // Ensure num_shapes doesn't exceed actual array count
        if (action.params.lfo.num_shapes > count) {
          action.params.lfo.num_shapes = (uint8_t)count;
        }
      }
    }
  }
  
  // Parse clock toggle/hold actions
  if (action.type == ACTION_CLOCK_TOGGLE || action.type == ACTION_CLOCK_HOLD) {
    cJSON* start_enabled = cJSON_GetObjectItem(obj, "start_enabled");
    // Default to false (press disables clock, since clock is running by default)
    action.params.clock.start_enabled = start_enabled ? cJSON_IsTrue(start_enabled) : false;
  }
  
  // Parse clock burst action
  if (action.type == ACTION_CLOCK_BURST) {
    cJSON* speed = cJSON_GetObjectItem(obj, "speed_percent");
    // Default to 100% (double the clock rate)
    action.params.clock_burst.speed_percent = speed ? speed->valueint : 100;
  }
  
  // Parse cut toggle/hold actions
  if (action.type == ACTION_CUT_TOGGLE || action.type == ACTION_CUT_HOLD) {
    cJSON* cut_mode = cJSON_GetObjectItem(obj, "cut_mode");
    if (cut_mode && cJSON_IsString(cut_mode)) {
      const char* mode_str = cut_mode->valuestring;
      if (strcmp(mode_str, "local") == 0) {
        action.params.cut.cut_mode = 0;
      } else if (strcmp(mode_str, "passthrough") == 0) {
        action.params.cut.cut_mode = 1;
      } else {
        // Default: "both"
        action.params.cut.cut_mode = 2;
      }
    } else {
      // Default to "both"
      action.params.cut.cut_mode = 2;
    }
  }
  
  // Parse confirm_pending action (Advanced mode target)
  if (action.type == ACTION_CONFIRM_PENDING) {
    cJSON* confirm_target = cJSON_GetObjectItem(obj, "confirm_target");
    if (confirm_target && cJSON_IsString(confirm_target)) {
      if (strcmp(confirm_target->valuestring, "scene") == 0) {
        action.params.confirm.target = CONFIRM_TARGET_SCENE;
      } else {
        action.params.confirm.target = CONFIRM_TARGET_PRESET;
      }
    } else {
      // Default to preset
      action.params.confirm.target = CONFIRM_TARGET_PRESET;
    }
  }
  
  // Parse timing (default: immediate)
  cJSON* timing = cJSON_GetObjectItem(obj, "timing");
  if (timing && cJSON_IsString(timing)) {
    action_timing_from_string(timing->valuestring, &action.timing, &action.timing_beat);
  }
  
  // Parse repeat settings (default: disabled)
  cJSON* repeat = cJSON_GetObjectItem(obj, "repeat");
  if (repeat && cJSON_IsBool(repeat)) {
    action.repeat_enabled = cJSON_IsTrue(repeat);
  }
  cJSON* repeat_div = cJSON_GetObjectItem(obj, "repeat_division");
  if (repeat_div && cJSON_IsString(repeat_div)) {
    action.repeat_division = action_repeat_division_from_string(repeat_div->valuestring);
  }
  
  // Parse probability (default: 100%)
  action.probability = 100;  // Default
  cJSON* prob = cJSON_GetObjectItem(obj, "probability");
  if (prob && cJSON_IsNumber(prob)) {
    int prob_val = prob->valueint;
    if (prob_val >= 10 && prob_val <= 100) {
      action.probability = (uint8_t)prob_val;
    }
  }
  
  // Parse pattern settings (default: disabled)
  action.pattern_length = 0;  // Default: no pattern
  action.pattern_mask = 0xFF; // Default: all steps enabled
  cJSON* pattern_len = cJSON_GetObjectItem(obj, "pattern_length");
  if (pattern_len && cJSON_IsNumber(pattern_len)) {
    int len = pattern_len->valueint;
    if (len >= 2 && len <= 8) {
      action.pattern_length = (uint8_t)len;
    }
  }
  cJSON* pattern_mask = cJSON_GetObjectItem(obj, "pattern_mask");
  if (pattern_mask && cJSON_IsNumber(pattern_mask)) {
    action.pattern_mask = (uint8_t)(pattern_mask->valueint & 0xFF);
  }
  
  // Parse morph settings (default: disabled, only for CONTROL_HOLD, CONTROL_CYCLE, RANDOMIZE)
  action.morph_enabled = false;
  action.morph_steps_mode = MORPH_STEPS_AUTO;
  action.morph_manual_steps = 32;
  action.morph_timing_mode = MORPH_TIMING_FEEL;
  action.morph_feel = MORPH_FEEL_MEDIUM;
  action.morph_division = MORPH_DIV_BAR;
  
  cJSON* morph = cJSON_GetObjectItem(obj, "morph");
  if (morph && cJSON_IsBool(morph)) {
    action.morph_enabled = cJSON_IsTrue(morph);
  }
  cJSON* morph_steps = cJSON_GetObjectItem(obj, "morph_steps");
  if (morph_steps && cJSON_IsString(morph_steps)) {
    action.morph_steps_mode = morph_steps_mode_from_string(morph_steps->valuestring);
  }
  cJSON* morph_manual = cJSON_GetObjectItem(obj, "morph_manual_steps");
  if (morph_manual && cJSON_IsNumber(morph_manual)) {
    int val = morph_manual->valueint;
    if (val >= 8 && val <= 128) {
      action.morph_manual_steps = (uint8_t)val;
    }
  }
  cJSON* morph_timing = cJSON_GetObjectItem(obj, "morph_timing");
  if (morph_timing && cJSON_IsString(morph_timing)) {
    action.morph_timing_mode = morph_timing_mode_from_string(morph_timing->valuestring);
  }
  cJSON* morph_feel = cJSON_GetObjectItem(obj, "morph_feel");
  if (morph_feel && cJSON_IsString(morph_feel)) {
    action.morph_feel = morph_feel_from_string(morph_feel->valuestring);
  }
  cJSON* morph_div = cJSON_GetObjectItem(obj, "morph_division");
  if (morph_div && cJSON_IsString(morph_div)) {
    action.morph_division = morph_division_from_string(morph_div->valuestring);
  }
  
  return action;
}

// Serialize on_load actions array to JSON
static cJSON* on_load_to_json(const scene_t* scene) {
  cJSON* array = cJSON_CreateArray();
  for (int i = 0; i < scene->num_on_load_actions && i < MAX_ON_LOAD_ACTIONS; i++) {
    cJSON* action_json = action_to_json(&scene->on_load[i]);
    if (action_json) {
      cJSON_AddItemToArray(array, action_json);
    }
  }
  return array;
}

// Parse on_load actions array from JSON
static void json_to_on_load(cJSON* array, scene_t* scene) {
  scene->num_on_load_actions = 0;
  if (!cJSON_IsArray(array)) return;
  
  int count = cJSON_GetArraySize(array);
  
  for (int i = 0; i < count && scene->num_on_load_actions < MAX_ON_LOAD_ACTIONS; i++) {
    action_t action = json_to_action(cJSON_GetArrayItem(array, i));
    if (action.type == ACTION_NONE) continue;
    
    // Validate action is allowed for on_load trigger
    if (!action_is_valid_for_trigger(action.type, ACTION_TRIGGER_ON_LOAD)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' in on_load",
        action_type_to_string(action.type));
      continue;
    }
    
    scene->on_load[scene->num_on_load_actions++] = action;
  }
}

// For backward compatibility: parse array format to single action (takes first action)
static action_t json_array_to_single_action(cJSON* array) {
  if (!cJSON_IsArray(array)) return (action_t){0};
  if (cJSON_GetArraySize(array) == 0) return (action_t){0};
  return json_to_action(cJSON_GetArrayItem(array, 0));
}

// Serialize continuous mapping to JSON
static cJSON* continuous_mapping_to_json(const continuous_mapping_t* mapping) {
  cJSON* obj = cJSON_CreateObject();
  
  cJSON_AddBoolToObject(obj, "enabled", mapping->enabled);
  cJSON_AddStringToObject(obj, "output_type", mapping->output_type == OUTPUT_TYPE_NOTE ? "note" : "cc");
  cJSON_AddNumberToObject(obj, "cc_number", mapping->cc_number);
  
  // Multi-CC array - always save all 4 slots to preserve positions
  // (0 values indicate inactive slots)
  if (mapping->num_cc_numbers > 0) {
    cJSON* cc_arr = cJSON_CreateArray();
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      cJSON_AddItemToArray(cc_arr, cJSON_CreateNumber(mapping->cc_numbers[i]));
    }
    cJSON_AddItemToObject(obj, "cc_numbers", cc_arr);
  }
  
  cJSON_AddNumberToObject(obj, "base_note", mapping->base_note);
  cJSON_AddNumberToObject(obj, "note_range", mapping->note_range);
  cJSON_AddNumberToObject(obj, "velocity", mapping->velocity);
  cJSON_AddBoolToObject(obj, "note_latch", mapping->note_latch);
  cJSON_AddNumberToObject(obj, "note_release_ms", mapping->note_release_ms);
  cJSON_AddStringToObject(obj, "polyphony", 
    mapping->polyphony == POLYPHONY_POLY ? "poly" : "mono");
  cJSON_AddNumberToObject(obj, "curve_type", mapping->curve.type);
  cJSON_AddNumberToObject(obj, "polarity", mapping->polarity);
  cJSON_AddNumberToObject(obj, "min_value", mapping->min_value);
  cJSON_AddNumberToObject(obj, "max_value", mapping->max_value);
  cJSON_AddBoolToObject(obj, "use_idle_value", mapping->use_idle_value);
  cJSON_AddNumberToObject(obj, "idle_value", mapping->idle_value);
  cJSON_AddNumberToObject(obj, "idle_timeout_ms", mapping->idle_timeout_ms);
  
  return obj;
}

// Deserialize continuous mapping from JSON
static void json_to_continuous_mapping(cJSON* obj, continuous_mapping_t* mapping) {
  if (!obj || !mapping) return;
  
  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) mapping->enabled = cJSON_IsTrue(enabled);
  
  cJSON* output_type = cJSON_GetObjectItem(obj, "output_type");
  if (output_type && cJSON_IsString(output_type)) {
    mapping->output_type = (strcmp(output_type->valuestring, "note") == 0) ? OUTPUT_TYPE_NOTE : OUTPUT_TYPE_CC;
  }
  
  cJSON* cc_num = cJSON_GetObjectItem(obj, "cc_number");
  if (cc_num) mapping->cc_number = cc_num->valueint;
  
  // Multi-CC array - load all slots and recalculate num_cc_numbers
  cJSON* cc_arr = cJSON_GetObjectItem(obj, "cc_numbers");
  if (cc_arr && cJSON_IsArray(cc_arr)) {
    int count = cJSON_GetArraySize(cc_arr);
    if (count > MAX_MULTI_CC) count = MAX_MULTI_CC;
    
    // Load values into slots
    for (int i = 0; i < count; i++) {
      cJSON* item = cJSON_GetArrayItem(cc_arr, i);
      if (item) mapping->cc_numbers[i] = (uint8_t)item->valueint;
    }
    // Clear remaining slots
    for (int i = count; i < MAX_MULTI_CC; i++) {
      mapping->cc_numbers[i] = 0;
    }
    
    // Calculate num_cc_numbers from non-zero entries
    mapping->num_cc_numbers = 0;
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      if (mapping->cc_numbers[i] > 0) {
        mapping->num_cc_numbers++;
      }
    }
  } else {
    mapping->num_cc_numbers = 0;
    for (int i = 0; i < MAX_MULTI_CC; i++) {
      mapping->cc_numbers[i] = 0;
    }
  }
  
  cJSON* base_note = cJSON_GetObjectItem(obj, "base_note");
  if (base_note) mapping->base_note = base_note->valueint;
  
  cJSON* note_range = cJSON_GetObjectItem(obj, "note_range");
  if (note_range) mapping->note_range = note_range->valueint;
  
  cJSON* velocity = cJSON_GetObjectItem(obj, "velocity");
  if (velocity) mapping->velocity = velocity->valueint;
  
  cJSON* note_latch = cJSON_GetObjectItem(obj, "note_latch");
  if (note_latch) mapping->note_latch = cJSON_IsTrue(note_latch);
  
  cJSON* note_release = cJSON_GetObjectItem(obj, "note_release_ms");
  if (note_release) mapping->note_release_ms = note_release->valueint;
  
  cJSON* polyphony_json = cJSON_GetObjectItem(obj, "polyphony");
  if (polyphony_json && cJSON_IsString(polyphony_json)) {
    mapping->polyphony = (strcmp(polyphony_json->valuestring, "poly") == 0) ? 
      POLYPHONY_POLY : POLYPHONY_MONO;
  }
  
  cJSON* curve_type = cJSON_GetObjectItem(obj, "curve_type");
  if (curve_type) mapping->curve = curve_create((curve_type_t)curve_type->valueint);
  
  cJSON* polarity = cJSON_GetObjectItem(obj, "polarity");
  if (polarity) mapping->polarity = (polarity_t)polarity->valueint;
  
  cJSON* min_val = cJSON_GetObjectItem(obj, "min_value");
  if (min_val) mapping->min_value = min_val->valueint;
  
  cJSON* max_val = cJSON_GetObjectItem(obj, "max_value");
  if (max_val) mapping->max_value = max_val->valueint;
  
  cJSON* use_idle = cJSON_GetObjectItem(obj, "use_idle_value");
  if (use_idle) mapping->use_idle_value = cJSON_IsTrue(use_idle);
  
  cJSON* idle_val = cJSON_GetObjectItem(obj, "idle_value");
  if (idle_val) mapping->idle_value = idle_val->valueint;
  
  cJSON* idle_timeout = cJSON_GetObjectItem(obj, "idle_timeout_ms");
  if (idle_timeout) mapping->idle_timeout_ms = idle_timeout->valueint;
}

// Serialize LFO config to JSON
static cJSON* lfo_config_to_json(const lfo_config_t* config) {
  cJSON* obj = cJSON_CreateObject();

  cJSON_AddBoolToObject(obj, "enabled", config->enabled);
  cJSON_AddStringToObject(obj, "waveform", lfo_waveform_to_string(config->waveform));
  cJSON_AddStringToObject(obj, "rate_mode", lfo_rate_mode_to_string(config->rate_mode));
  cJSON_AddStringToObject(obj, "start_mode", lfo_start_mode_to_string(config->start_mode));
  cJSON_AddStringToObject(obj, "trigger_timing", lfo_trigger_timing_to_string(config->trigger_timing));
  cJSON_AddBoolToObject(obj, "repeat", config->repeat);
  cJSON_AddBoolToObject(obj, "reset_phase", config->reset_phase);
  cJSON_AddBoolToObject(obj, "restore_on_stop", config->restore_on_stop);
  cJSON_AddNumberToObject(obj, "rate_hz", config->rate_hz_x100 / 100.0);
  cJSON_AddStringToObject(obj, "division", lfo_division_to_string(config->division));
  cJSON_AddNumberToObject(obj, "phase_offset", config->phase_offset);
  cJSON_AddNumberToObject(obj, "duty_cycle", config->duty_cycle);
  cJSON_AddNumberToObject(obj, "floor", config->floor);
  cJSON_AddNumberToObject(obj, "ceiling", config->ceiling);
  cJSON_AddStringToObject(obj, "resolution", lfo_resolution_mode_to_string(config->resolution_mode));
  cJSON_AddNumberToObject(obj, "manual_steps", config->manual_steps);

  return obj;
}

// Deserialize LFO config from JSON
static void json_to_lfo_config(cJSON* obj, lfo_config_t* config) {
  if (!obj || !config) return;
  
  cJSON* enabled = cJSON_GetObjectItem(obj, "enabled");
  if (enabled) config->enabled = cJSON_IsTrue(enabled);
  
  cJSON* waveform = cJSON_GetObjectItem(obj, "waveform");
  if (waveform && cJSON_IsString(waveform)) {
    config->waveform = lfo_waveform_from_string(waveform->valuestring);
  }
  
  cJSON* rate_mode = cJSON_GetObjectItem(obj, "rate_mode");
  if (rate_mode && cJSON_IsString(rate_mode)) {
    config->rate_mode = lfo_rate_mode_from_string(rate_mode->valuestring);
  }

  cJSON* start_mode = cJSON_GetObjectItem(obj, "start_mode");
  if (start_mode && cJSON_IsString(start_mode)) {
    config->start_mode = lfo_start_mode_from_string(start_mode->valuestring);
  }

  cJSON* trigger_timing = cJSON_GetObjectItem(obj, "trigger_timing");
  if (trigger_timing && cJSON_IsString(trigger_timing)) {
    config->trigger_timing = lfo_trigger_timing_from_string(trigger_timing->valuestring);
  }

  cJSON* repeat = cJSON_GetObjectItem(obj, "repeat");
  if (repeat) config->repeat = cJSON_IsTrue(repeat);

  cJSON* reset_phase = cJSON_GetObjectItem(obj, "reset_phase");
  if (reset_phase) config->reset_phase = cJSON_IsTrue(reset_phase);

  cJSON* restore_on_stop = cJSON_GetObjectItem(obj, "restore_on_stop");
  if (restore_on_stop) config->restore_on_stop = cJSON_IsTrue(restore_on_stop);

  cJSON* rate_hz = cJSON_GetObjectItem(obj, "rate_hz");
  if (rate_hz && cJSON_IsNumber(rate_hz)) {
    float hz = (float)rate_hz->valuedouble;
    if (hz < 0.05f) hz = 0.05f;
    if (hz > 20.0f) hz = 20.0f;
    config->rate_hz_x100 = (uint16_t)(hz * 100.0f);
  }
  
  cJSON* division = cJSON_GetObjectItem(obj, "division");
  if (division && cJSON_IsString(division)) {
    config->division = lfo_division_from_string(division->valuestring);
  }
  
  cJSON* phase_offset = cJSON_GetObjectItem(obj, "phase_offset");
  if (phase_offset) config->phase_offset = (uint8_t)phase_offset->valueint;

  cJSON* duty_cycle = cJSON_GetObjectItem(obj, "duty_cycle");
  if (duty_cycle) config->duty_cycle = (uint8_t)duty_cycle->valueint;

  cJSON* floor_val = cJSON_GetObjectItem(obj, "floor");
  if (floor_val) config->floor = (uint8_t)floor_val->valueint;

  cJSON* ceiling_val = cJSON_GetObjectItem(obj, "ceiling");
  if (ceiling_val) config->ceiling = (uint8_t)ceiling_val->valueint;

  cJSON* resolution = cJSON_GetObjectItem(obj, "resolution");
  if (resolution && cJSON_IsString(resolution)) {
    config->resolution_mode = lfo_resolution_mode_from_string(resolution->valuestring);
  }

  cJSON* manual_steps = cJSON_GetObjectItem(obj, "manual_steps");
  if (manual_steps) {
    uint8_t steps = (uint8_t)manual_steps->valueint;
    // Clamp to valid values
    if (steps <= 16) steps = 16;
    else if (steps <= 32) steps = 32;
    else if (steps <= 64) steps = 64;
    else steps = 128;
    config->manual_steps = steps;
  }
}

// Scene JSON serialization
static cJSON* scene_to_json(const scene_t* scene) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "name", scene->name);

  // Only write device_id if it's set (non-empty)
  if (scene->device_id[0] != '\0') {
    cJSON_AddStringToObject(root, "device_id", scene->device_id);
  }

  cJSON_AddNumberToObject(root, "program_number", scene->program_number);
  cJSON_AddBoolToObject(root, "send_pc_on_load", scene->send_pc_on_load);
  
  // Serialize touchwheel mode, style, and continuous mapping
  const char* tw_mode_str;
  switch (scene->touchwheel_mode) {
    case TOUCHWHEEL_MODE_PADS: tw_mode_str = "pads"; break;
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: tw_mode_str = "program_change"; break;
    case TOUCHWHEEL_MODE_SET_TEMPO: tw_mode_str = "set_tempo"; break;
    case TOUCHWHEEL_MODE_PITCH_BEND: tw_mode_str = "pitch_bend"; break;
    case TOUCHWHEEL_MODE_AFTERTOUCH: tw_mode_str = "aftertouch"; break;
    case TOUCHWHEEL_MODE_DOUBLE_CC: tw_mode_str = "double_cc"; break;
    case TOUCHWHEEL_MODE_VELOCITY: tw_mode_str = "velocity"; break;
    case TOUCHWHEEL_MODE_LFO_RATE: tw_mode_str = "lfo_rate"; break;
    default: tw_mode_str = "continuous"; break;
  }
  cJSON_AddStringToObject(root, "touchwheel_mode", tw_mode_str);
  const char* tw_style_str = (scene->touchwheel_style == TOUCHWHEEL_STYLE_BIPOLAR) ? "bipolar" :
                             (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? "endless" : "odometer";
  cJSON_AddStringToObject(root, "touchwheel_style", tw_style_str);
  cJSON_AddItemToObject(root, "touchwheel", continuous_mapping_to_json(&scene->touchwheel));
  
  cJSON* touchpads = cJSON_CreateArray();
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    cJSON* pad = cJSON_CreateObject();
    cJSON_AddBoolToObject(pad, "enabled", scene->touchpads[i].enabled);
    cJSON* action_json = action_to_json(&scene->touchpads[i].action);
    if (action_json) {
      cJSON_AddItemToObject(pad, "action", action_json);
    }
    cJSON_AddItemToArray(touchpads, pad);
  }
  cJSON_AddItemToObject(root, "touchpads", touchpads);
  
  // on_load is an array (up to 4 actions)
  cJSON_AddItemToObject(root, "on_load", on_load_to_json(scene));
  
  // Discrete inputs are single actions
  cJSON* btn_l = action_to_json(&scene->button_left);
  if (btn_l) cJSON_AddItemToObject(root, "button_left", btn_l);
  cJSON* btn_r = action_to_json(&scene->button_right);
  if (btn_r) cJSON_AddItemToObject(root, "button_right", btn_r);
  cJSON* btn_b = action_to_json(&scene->button_both);
  if (btn_b) cJSON_AddItemToObject(root, "button_both", btn_b);
  cJSON* bump_json = action_to_json(&scene->bump);
  if (bump_json) cJSON_AddItemToObject(root, "bump", bump_json);
  
  // Serialize continuous mappings
  cJSON_AddItemToObject(root, "expression", continuous_mapping_to_json(&scene->expression));
  cJSON_AddItemToObject(root, "cv", continuous_mapping_to_json(&scene->cv));
  cJSON_AddItemToObject(root, "proximity", continuous_mapping_to_json(&scene->proximity));
  cJSON_AddItemToObject(root, "als", continuous_mapping_to_json(&scene->als));
  
  // Serialize expression jack mode and pedal actions
  const char* mode_str = (scene->expression_mode == EXPRESSION_MODE_NONE) ? "none" :
                         (scene->expression_mode == EXPRESSION_MODE_PEDAL) ? "expression" :
                         (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) ? "sustain" :
                         (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO) ? "sostenuto" :
                         (scene->expression_mode == EXPRESSION_MODE_SWITCH) ? "switch" : "gate";
  cJSON_AddStringToObject(root, "expression_mode", mode_str);
  cJSON* sustain_json = action_to_json(&scene->sustain);
  if (sustain_json) cJSON_AddItemToObject(root, "sustain", sustain_json);
  cJSON* sostenuto_json = action_to_json(&scene->sostenuto);
  if (sostenuto_json) cJSON_AddItemToObject(root, "sostenuto", sostenuto_json);
  cJSON* expr_sw_json = action_to_json(&scene->expr_switch);
  if (expr_sw_json) cJSON_AddItemToObject(root, "expr_switch", expr_sw_json);
  
  // Serialize CV input mode
  const char* cv_mode_str = (scene->cv_input_mode == INPUT_MODE_NONE) ? "none" :
                            (scene->cv_input_mode == INPUT_MODE_CV) ? "cv" :
                            (scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC) ? "clock_sync" :
                            (scene->cv_input_mode == INPUT_MODE_AUDIO) ? "audio" : "note";
  cJSON_AddStringToObject(root, "cv_input_mode", cv_mode_str);
  
  // Serialize velocity mode settings (helper inline)
  #define VEL_MODE_STR(m) ((m) == VELOCITY_MODE_GATE_VOLTAGE ? "gate_voltage" : \
                           (m) == VELOCITY_MODE_TOUCHWHEEL ? "touchwheel" : "fixed")
  
  // CV velocity mode and value
  cJSON_AddStringToObject(root, "cv_velocity_mode", VEL_MODE_STR(scene->cv_velocity_mode));
  cJSON_AddNumberToObject(root, "cv_velocity", scene->cv_velocity);
  
  // Audio envelope follower configuration
  cJSON* audio_config = cJSON_CreateObject();
  const char* audio_range_str = (scene->audio_config.range == CV_RANGE_BIPOLAR_10V) ? "bi10v" : "bi5v";
  cJSON_AddStringToObject(audio_config, "range", audio_range_str);
  cJSON_AddNumberToObject(audio_config, "sensitivity", scene->audio_config.sensitivity);
  cJSON_AddNumberToObject(audio_config, "attack_ms", scene->audio_config.attack_ms);
  cJSON_AddNumberToObject(audio_config, "release_ms", scene->audio_config.release_ms);
  cJSON_AddNumberToObject(audio_config, "threshold", scene->audio_config.threshold);
  const char* audio_pol_str = (scene->audio_config.polarity == AUDIO_POLARITY_REPEL) ? "repel" : "attract";
  cJSON_AddStringToObject(audio_config, "polarity", audio_pol_str);
  cJSON_AddItemToObject(root, "audio_config", audio_config);
  
  // Other continuous input velocity modes
  cJSON_AddStringToObject(root, "expression_velocity_mode", VEL_MODE_STR(scene->expression_velocity_mode));
  cJSON_AddStringToObject(root, "proximity_velocity_mode", VEL_MODE_STR(scene->proximity_velocity_mode));
  cJSON_AddStringToObject(root, "als_velocity_mode", VEL_MODE_STR(scene->als_velocity_mode));
  
  #undef VEL_MODE_STR
  
  // Serialize tempo settings
  cJSON_AddNumberToObject(root, "bpm", scene->bpm);
  
  const char* clock_src_str = (scene->clock_source == CLOCK_SOURCE_INTERNAL) ? "internal" :
                              (scene->clock_source == CLOCK_SOURCE_MIDI) ? "midi" : "sync";
  cJSON_AddStringToObject(root, "clock_source", clock_src_str);
  
  const char* beat_div_str = (scene->beat_divider == DIVIDER_QUARTER) ? "quarter" :
                             (scene->beat_divider == DIVIDER_EIGHTH) ? "eighth" : "sixteenth";
  cJSON_AddStringToObject(root, "beat_divider", beat_div_str);
  
  cJSON* time_sig = cJSON_CreateObject();
  cJSON_AddNumberToObject(time_sig, "numerator", scene->time_signature.numerator);
  cJSON_AddNumberToObject(time_sig, "denominator", scene->time_signature.denominator);
  cJSON_AddItemToObject(root, "time_signature", time_sig);
  
  cJSON_AddBoolToObject(root, "use_transport", scene->use_transport);
  cJSON_AddBoolToObject(root, "send_clock", scene->send_clock);
  
  // Serialize LFO configurations and mappings
  cJSON_AddItemToObject(root, "lfo1_config", lfo_config_to_json(&scene->lfo1_config));
  cJSON_AddItemToObject(root, "lfo2_config", lfo_config_to_json(&scene->lfo2_config));
  cJSON_AddItemToObject(root, "lfo1", continuous_mapping_to_json(&scene->lfo1));
  cJSON_AddItemToObject(root, "lfo2", continuous_mapping_to_json(&scene->lfo2));
  
  // Serialize LFO velocity modes
  const char* lfo1_vel_str = (scene->lfo1_velocity_mode == VELOCITY_MODE_FIXED) ? "fixed" :
                             (scene->lfo1_velocity_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "gate_voltage" : "touchwheel";
  cJSON_AddStringToObject(root, "lfo1_velocity_mode", lfo1_vel_str);
  
  const char* lfo2_vel_str = (scene->lfo2_velocity_mode == VELOCITY_MODE_FIXED) ? "fixed" :
                             (scene->lfo2_velocity_mode == VELOCITY_MODE_GATE_VOLTAGE) ? "gate_voltage" : "touchwheel";
  cJSON_AddStringToObject(root, "lfo2_velocity_mode", lfo2_vel_str);
  
  return root;
}

static esp_err_t json_to_scene(cJSON* root, scene_t* scene) {
  if (!root || !scene) return ESP_ERR_INVALID_ARG;

  cJSON* name = cJSON_GetObjectItem(root, "name");
  if (name && cJSON_IsString(name)) {
    strncpy(scene->name, name->valuestring, sizeof(scene->name) - 1);
    scene->name[sizeof(scene->name) - 1] = '\0';
  }

  // Parse device_id (optional - empty means use global device_config)
  cJSON* device_id = cJSON_GetObjectItem(root, "device_id");
  if (device_id && cJSON_IsString(device_id)) {
    strncpy(scene->device_id, device_id->valuestring, sizeof(scene->device_id) - 1);
    scene->device_id[sizeof(scene->device_id) - 1] = '\0';
  } else {
    scene->device_id[0] = '\0';  // Use global
  }

  cJSON* program = cJSON_GetObjectItem(root, "program_number");
  if (program) scene->program_number = program->valueint;
  
  // Support both old and new key names for backward compatibility
  cJSON* send_pc = cJSON_GetObjectItem(root, "send_pc_on_load");
  if (!send_pc) send_pc = cJSON_GetObjectItem(root, "send_pc_on_change");  // Legacy
  if (send_pc) scene->send_pc_on_load = cJSON_IsTrue(send_pc);
  
  // Deserialize touchwheel mode
  cJSON* tw_mode = cJSON_GetObjectItem(root, "touchwheel_mode");
  if (tw_mode && cJSON_IsString(tw_mode)) {
    const char* mode_str = tw_mode->valuestring;
    if (strcmp(mode_str, "pads") == 0 || strcmp(mode_str, "buttons") == 0) {
      scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;  // "buttons" for backwards compatibility
    }
    else if (strcmp(mode_str, "program_change") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
    else if (strcmp(mode_str, "continuous") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_CONTINUOUS;
    else if (strcmp(mode_str, "set_tempo") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_SET_TEMPO;
    else if (strcmp(mode_str, "pitch_bend") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_PITCH_BEND;
    else if (strcmp(mode_str, "aftertouch") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_AFTERTOUCH;
    else if (strcmp(mode_str, "double_cc") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_DOUBLE_CC;
    else if (strcmp(mode_str, "velocity") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_VELOCITY;
    else if (strcmp(mode_str, "lfo_rate") == 0) scene->touchwheel_mode = TOUCHWHEEL_MODE_LFO_RATE;
    // Legacy: nrpn/rpn modes removed, map to continuous for backwards compatibility
    else if (strcmp(mode_str, "nrpn") == 0 || strcmp(mode_str, "rpn") == 0) {
      scene->touchwheel_mode = TOUCHWHEEL_MODE_CONTINUOUS;
    }
    else scene->touchwheel_mode = TOUCHWHEEL_MODE_PADS;
  }
  
  // Deserialize touchwheel style (odometer, endless, or bipolar)
  cJSON* tw_style = cJSON_GetObjectItem(root, "touchwheel_style");
  if (tw_style && cJSON_IsString(tw_style)) {
    const char* style_str = tw_style->valuestring;
    if (strcmp(style_str, "endless") == 0) scene->touchwheel_style = TOUCHWHEEL_STYLE_ENDLESS;
    else if (strcmp(style_str, "bipolar") == 0) scene->touchwheel_style = TOUCHWHEEL_STYLE_BIPOLAR;
    else scene->touchwheel_style = TOUCHWHEEL_STYLE_ODOMETER;
  }
  
  // Deserialize touchwheel continuous mapping
  cJSON* touchwheel = cJSON_GetObjectItem(root, "touchwheel");
  if (touchwheel) json_to_continuous_mapping(touchwheel, &scene->touchwheel);
  
  cJSON* touchpads = cJSON_GetObjectItem(root, "touchpads");
  if (touchpads && cJSON_IsArray(touchpads)) {
    int count = cJSON_GetArraySize(touchpads);
    for (int i = 0; i < count && i < NUM_TOUCHPADS; i++) {
      cJSON* pad = cJSON_GetArrayItem(touchpads, i);
      cJSON* enabled = cJSON_GetObjectItem(pad, "enabled");
      if (enabled) scene->touchpads[i].enabled = cJSON_IsTrue(enabled);
      
      // Determine trigger type based on pad index
      action_trigger_type_t trigger = (i <= 7) ?
        ACTION_TRIGGER_TOUCHPAD_0_7 : ACTION_TRIGGER_TOUCHPAD_8_11;
      
      // Try new format first (single "action"), fall back to old format ("actions" array)
      cJSON* action = cJSON_GetObjectItem(pad, "action");
      action_t parsed_action = {0};
      if (action) {
        parsed_action = json_to_action(action);
      } else {
        cJSON* actions = cJSON_GetObjectItem(pad, "actions");
        if (actions) parsed_action = json_array_to_single_action(actions);
      }
      
      // Validate action is allowed for this pad's trigger type
      if (parsed_action.type != ACTION_NONE &&
          !action_is_valid_for_trigger(parsed_action.type, trigger)) {
        ESP_LOGW(TAG, "Ignoring invalid action '%s' on pad %d",
          action_type_to_string(parsed_action.type), i);
        parsed_action.type = ACTION_NONE;
      }
      scene->touchpads[i].action = parsed_action;
    }
  }
  
  // on_load is an array of up to 4 actions
  cJSON* on_load = cJSON_GetObjectItem(root, "on_load");
  if (on_load) json_to_on_load(on_load, scene);
  
  // Discrete inputs: try object first (new format), fall back to array (old format)
  cJSON* btn_l = cJSON_GetObjectItem(root, "button_left");
  if (btn_l) {
    action_t action = cJSON_IsArray(btn_l) ?
      json_array_to_single_action(btn_l) : json_to_action(btn_l);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_left",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_left = action;
  }
  
  cJSON* btn_r = cJSON_GetObjectItem(root, "button_right");
  if (btn_r) {
    action_t action = cJSON_IsArray(btn_r) ?
      json_array_to_single_action(btn_r) : json_to_action(btn_r);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_right",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_right = action;
  }
  
  cJSON* btn_both = cJSON_GetObjectItem(root, "button_both");
  if (btn_both) {
    action_t action = cJSON_IsArray(btn_both) ?
      json_array_to_single_action(btn_both) : json_to_action(btn_both);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for button_both",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->button_both = action;
  }
  
  cJSON* bump = cJSON_GetObjectItem(root, "bump");
  if (bump) {
    action_t bump_action = cJSON_IsArray(bump) ?
      json_array_to_single_action(bump) : json_to_action(bump);
    if (bump_action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(bump_action.type, ACTION_TRIGGER_BUMP)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for bump",
        action_type_to_string(bump_action.type));
      bump_action.type = ACTION_NONE;
    }
    scene->bump = bump_action;
  }
  
  // Deserialize continuous mappings
  cJSON* expression = cJSON_GetObjectItem(root, "expression");
  if (expression) json_to_continuous_mapping(expression, &scene->expression);
  
  cJSON* cv = cJSON_GetObjectItem(root, "cv");
  if (cv) json_to_continuous_mapping(cv, &scene->cv);
  
  cJSON* proximity = cJSON_GetObjectItem(root, "proximity");
  if (proximity) json_to_continuous_mapping(proximity, &scene->proximity);
  
  cJSON* als = cJSON_GetObjectItem(root, "als");
  if (als) json_to_continuous_mapping(als, &scene->als);
  
  // Deserialize expression jack mode
  cJSON* expr_mode = cJSON_GetObjectItem(root, "expression_mode");
  if (expr_mode && cJSON_IsString(expr_mode)) {
    const char* mode_str = expr_mode->valuestring;
    if (strcmp(mode_str, "none") == 0) scene->expression_mode = EXPRESSION_MODE_NONE;
    else if (strcmp(mode_str, "sustain") == 0) scene->expression_mode = EXPRESSION_MODE_SUSTAIN;
    else if (strcmp(mode_str, "sostenuto") == 0) scene->expression_mode = EXPRESSION_MODE_SOSTENUTO;
    else if (strcmp(mode_str, "gate") == 0) scene->expression_mode = EXPRESSION_MODE_GATE;
    else if (strcmp(mode_str, "switch") == 0) scene->expression_mode = EXPRESSION_MODE_SWITCH;
    else scene->expression_mode = EXPRESSION_MODE_PEDAL;
  }
  
  // Note: expression.enabled is loaded from JSON by json_to_continuous_mapping()
  // and should NOT be overwritten here. The auto-management of enabled only
  // happens when the mode is actively changed via scene_set_expression_mode().
  
  // Deserialize pedal actions (try object first, fall back to array for backward compat)
  // Sustain/sostenuto are essentially button-like triggers (press/release)
  cJSON* sustain = cJSON_GetObjectItem(root, "sustain");
  if (sustain) {
    action_t action = cJSON_IsArray(sustain) ?
      json_array_to_single_action(sustain) : json_to_action(sustain);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for sustain",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->sustain = action;
  }
  
  cJSON* sostenuto = cJSON_GetObjectItem(root, "sostenuto");
  if (sostenuto) {
    action_t action = cJSON_IsArray(sostenuto) ?
      json_array_to_single_action(sostenuto) : json_to_action(sostenuto);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_BUTTON)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for sostenuto",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->sostenuto = action;
  }
  
  cJSON* expr_switch_json = cJSON_GetObjectItem(root, "expr_switch");
  if (expr_switch_json) {
    action_t action = cJSON_IsArray(expr_switch_json) ?
      json_array_to_single_action(expr_switch_json) : json_to_action(expr_switch_json);
    if (action.type != ACTION_NONE &&
        !action_is_valid_for_trigger(action.type, ACTION_TRIGGER_EXPR_SWITCH)) {
      ESP_LOGW(TAG, "Ignoring invalid action '%s' for expr_switch",
        action_type_to_string(action.type));
      action.type = ACTION_NONE;
    }
    scene->expr_switch = action;
  }
  
  // Deserialize CV input mode
  cJSON* cv_mode = cJSON_GetObjectItem(root, "cv_input_mode");
  if (cv_mode && cJSON_IsString(cv_mode)) {
    const char* mode_str = cv_mode->valuestring;
    if (strcmp(mode_str, "none") == 0) scene->cv_input_mode = INPUT_MODE_NONE;
    else if (strcmp(mode_str, "clock_sync") == 0) scene->cv_input_mode = INPUT_MODE_CLOCK_SYNC;
    else if (strcmp(mode_str, "audio") == 0) scene->cv_input_mode = INPUT_MODE_AUDIO;
    else if (strcmp(mode_str, "note") == 0) scene->cv_input_mode = INPUT_MODE_NOTE;
    else scene->cv_input_mode = INPUT_MODE_CV;
  }
  
  // Auto-manage cv.enabled based on cv_input_mode (same logic as scene_set_cv_input_mode)
  // This ensures cv_input_mode is the source of truth, regardless of what the JSON cv.enabled says
  if (scene->cv_input_mode == INPUT_MODE_NONE) {
    scene->cv.enabled = false;
  } else if (scene->cv_input_mode == INPUT_MODE_CV) {
    scene->cv.enabled = true;
  }
  // INPUT_MODE_NOTE: enabled is managed by input_manager at runtime
  // INPUT_MODE_CLOCK_SYNC: CV is used for tempo, not continuous routing
  
  // Helper to parse velocity mode from JSON string
  #define PARSE_VEL_MODE(json_obj, target) do { \
    if ((json_obj) && cJSON_IsString(json_obj)) { \
      const char* _mode_str = (json_obj)->valuestring; \
      if (strcmp(_mode_str, "gate_voltage") == 0) (target) = VELOCITY_MODE_GATE_VOLTAGE; \
      else if (strcmp(_mode_str, "touchwheel") == 0) (target) = VELOCITY_MODE_TOUCHWHEEL; \
      else (target) = VELOCITY_MODE_FIXED; \
    } \
  } while(0)
  
  // Deserialize CV velocity mode settings (check both old and new field names)
  cJSON* cv_vel_mode = cJSON_GetObjectItem(root, "cv_velocity_mode");
  if (!cv_vel_mode) cv_vel_mode = cJSON_GetObjectItem(root, "note_velocity_mode");  // Backward compat
  PARSE_VEL_MODE(cv_vel_mode, scene->cv_velocity_mode);
  
  cJSON* cv_vel = cJSON_GetObjectItem(root, "cv_velocity");
  if (!cv_vel) cv_vel = cJSON_GetObjectItem(root, "note_fixed_velocity");  // Backward compat
  if (cv_vel && cJSON_IsNumber(cv_vel)) {
    int vel = cv_vel->valueint;
    if (vel >= 1 && vel <= 127) scene->cv_velocity = (uint8_t)vel;
  }
  
  // Deserialize audio envelope follower configuration
  cJSON* audio_cfg = cJSON_GetObjectItem(root, "audio_config");
  if (audio_cfg && cJSON_IsObject(audio_cfg)) {
    cJSON* range_json = cJSON_GetObjectItem(audio_cfg, "range");
    if (range_json && cJSON_IsString(range_json)) {
      if (strcmp(range_json->valuestring, "bi10v") == 0) {
        scene->audio_config.range = CV_RANGE_BIPOLAR_10V;
      } else {
        scene->audio_config.range = CV_RANGE_BIPOLAR_5V;
      }
    }
    cJSON* sens = cJSON_GetObjectItem(audio_cfg, "sensitivity");
    if (sens && cJSON_IsNumber(sens)) {
      scene->audio_config.sensitivity = (uint8_t)sens->valueint;
    }
    cJSON* attack = cJSON_GetObjectItem(audio_cfg, "attack_ms");
    if (attack && cJSON_IsNumber(attack)) {
      int val = attack->valueint;
      if (val < 5) val = 5;
      if (val > 100) val = 100;
      scene->audio_config.attack_ms = (uint16_t)val;
    }
    cJSON* release = cJSON_GetObjectItem(audio_cfg, "release_ms");
    if (release && cJSON_IsNumber(release)) {
      int val = release->valueint;
      if (val < 50) val = 50;
      if (val > 2000) val = 2000;
      scene->audio_config.release_ms = (uint16_t)val;
    }
    cJSON* thresh = cJSON_GetObjectItem(audio_cfg, "threshold");
    if (thresh && cJSON_IsNumber(thresh)) {
      int val = thresh->valueint;
      if (val < 0) val = 0;
      if (val > 127) val = 127;
      scene->audio_config.threshold = (uint8_t)val;
    }
    cJSON* pol = cJSON_GetObjectItem(audio_cfg, "polarity");
    if (pol && cJSON_IsString(pol)) {
      if (strcmp(pol->valuestring, "repel") == 0) {
        scene->audio_config.polarity = AUDIO_POLARITY_REPEL;
      } else {
        scene->audio_config.polarity = AUDIO_POLARITY_ATTRACT;
      }
    }
  }
  
  // Deserialize other velocity modes
  PARSE_VEL_MODE(cJSON_GetObjectItem(root, "expression_velocity_mode"), scene->expression_velocity_mode);
  PARSE_VEL_MODE(cJSON_GetObjectItem(root, "proximity_velocity_mode"), scene->proximity_velocity_mode);
  PARSE_VEL_MODE(cJSON_GetObjectItem(root, "als_velocity_mode"), scene->als_velocity_mode);
  
  #undef PARSE_VEL_MODE
  
  // Deserialize tempo settings
  cJSON* bpm_json = cJSON_GetObjectItem(root, "bpm");
  if (bpm_json && cJSON_IsNumber(bpm_json)) {
    int bpm = bpm_json->valueint;
    if (bpm >= 20 && bpm <= 300) scene->bpm = (uint16_t)bpm;
  }
  
  cJSON* clock_src = cJSON_GetObjectItem(root, "clock_source");
  if (clock_src && cJSON_IsString(clock_src)) {
    const char* src_str = clock_src->valuestring;
    if (strcmp(src_str, "midi") == 0) scene->clock_source = CLOCK_SOURCE_MIDI;
    else if (strcmp(src_str, "sync") == 0) scene->clock_source = CLOCK_SOURCE_SYNC;
    else scene->clock_source = CLOCK_SOURCE_INTERNAL;
  }
  
  cJSON* beat_div = cJSON_GetObjectItem(root, "beat_divider");
  if (beat_div && cJSON_IsString(beat_div)) {
    const char* div_str = beat_div->valuestring;
    if (strcmp(div_str, "eighth") == 0) scene->beat_divider = DIVIDER_EIGHTH;
    else if (strcmp(div_str, "sixteenth") == 0) scene->beat_divider = DIVIDER_SIXTEENTH;
    else scene->beat_divider = DIVIDER_QUARTER;
  }
  
  cJSON* time_sig = cJSON_GetObjectItem(root, "time_signature");
  if (time_sig && cJSON_IsObject(time_sig)) {
    cJSON* numerator = cJSON_GetObjectItem(time_sig, "numerator");
    cJSON* denominator = cJSON_GetObjectItem(time_sig, "denominator");
    if (numerator) scene->time_signature.numerator = numerator->valueint;
    if (denominator) scene->time_signature.denominator = denominator->valueint;
  }
  
  cJSON* use_transport = cJSON_GetObjectItem(root, "use_transport");
  if (use_transport && cJSON_IsBool(use_transport)) {
    scene->use_transport = cJSON_IsTrue(use_transport);
  }

  cJSON* send_clock = cJSON_GetObjectItem(root, "send_clock");
  if (send_clock && cJSON_IsBool(send_clock)) {
    scene->send_clock = cJSON_IsTrue(send_clock);
  }

  // Deserialize LFO configurations and mappings
  cJSON* lfo1_config = cJSON_GetObjectItem(root, "lfo1_config");
  if (lfo1_config) json_to_lfo_config(lfo1_config, &scene->lfo1_config);
  
  cJSON* lfo2_config = cJSON_GetObjectItem(root, "lfo2_config");
  if (lfo2_config) json_to_lfo_config(lfo2_config, &scene->lfo2_config);
  
  cJSON* lfo1_mapping = cJSON_GetObjectItem(root, "lfo1");
  if (lfo1_mapping) json_to_continuous_mapping(lfo1_mapping, &scene->lfo1);

  cJSON* lfo2_mapping = cJSON_GetObjectItem(root, "lfo2");
  if (lfo2_mapping) json_to_continuous_mapping(lfo2_mapping, &scene->lfo2);

  // Force LFO curves to LINEAR (curves don't apply to LFO waveforms)
  // Also force min/max to 0/127 - floor/ceiling is now applied in lfo_config at full resolution
  scene->lfo1.curve.type = CURVE_LINEAR;
  scene->lfo1.min_value = 0;
  scene->lfo1.max_value = 127;
  scene->lfo2.curve.type = CURVE_LINEAR;
  scene->lfo2.min_value = 0;
  scene->lfo2.max_value = 127;

  // Deserialize LFO velocity modes
  cJSON* lfo1_vel_mode = cJSON_GetObjectItem(root, "lfo1_velocity_mode");
  if (lfo1_vel_mode && cJSON_IsString(lfo1_vel_mode)) {
    const char* s = lfo1_vel_mode->valuestring;
    if (strcmp(s, "fixed") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_FIXED;
    else if (strcmp(s, "gate_voltage") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
    else if (strcmp(s, "touchwheel") == 0) scene->lfo1_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  }
  
  cJSON* lfo2_vel_mode = cJSON_GetObjectItem(root, "lfo2_velocity_mode");
  if (lfo2_vel_mode && cJSON_IsString(lfo2_vel_mode)) {
    const char* s = lfo2_vel_mode->valuestring;
    if (strcmp(s, "fixed") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_FIXED;
    else if (strcmp(s, "gate_voltage") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
    else if (strcmp(s, "touchwheel") == 0) scene->lfo2_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  }

  return ESP_OK;
}

// Scene storage implementation
esp_err_t scene_load_from_flash(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return ESP_ERR_NOT_FOUND;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;
  
  // Load into cache
  int cache_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  // Initialize with defaults first to ensure all fields have valid values,
  // even if JSON file is missing newer fields (e.g., velocity modes)
  scene_init_defaults(&g_scene_manager.cache[cache_idx].scene, scene_index);
  esp_err_t ret = json_to_scene(root, &g_scene_manager.cache[cache_idx].scene);
  cJSON_Delete(root);
  
  if (ret == ESP_OK) {
    g_scene_manager.cache[cache_idx].index = scene_index;
    g_scene_manager.cache[cache_idx].valid = true;
  }
  
  return ret;
}

// Read persisted touchwheel_mode from JSON without loading entire scene
// Used by menu to show configured (not runtime) value
touchwheel_mode_t scene_get_persisted_touchwheel_mode(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return TOUCHWHEEL_MODE_PADS;  // Default if file not found
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return TOUCHWHEEL_MODE_PADS; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return TOUCHWHEEL_MODE_PADS;
  
  touchwheel_mode_t mode = TOUCHWHEEL_MODE_PADS;
  cJSON* tw_mode = cJSON_GetObjectItem(root, "touchwheel_mode");
  if (tw_mode && cJSON_IsString(tw_mode)) {
    const char* mode_str = tw_mode->valuestring;
    if (strcmp(mode_str, "pads") == 0 || strcmp(mode_str, "buttons") == 0) {
      mode = TOUCHWHEEL_MODE_PADS;
    } else if (strcmp(mode_str, "program_change") == 0) {
      mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
    } else if (strcmp(mode_str, "continuous") == 0) {
      mode = TOUCHWHEEL_MODE_CONTINUOUS;
    } else if (strcmp(mode_str, "set_tempo") == 0) {
      mode = TOUCHWHEEL_MODE_SET_TEMPO;
    } else if (strcmp(mode_str, "pitch_bend") == 0) {
      mode = TOUCHWHEEL_MODE_PITCH_BEND;
    } else if (strcmp(mode_str, "aftertouch") == 0) {
      mode = TOUCHWHEEL_MODE_AFTERTOUCH;
    } else if (strcmp(mode_str, "double_cc") == 0) {
      mode = TOUCHWHEEL_MODE_DOUBLE_CC;
    } else if (strcmp(mode_str, "velocity") == 0) {
      mode = TOUCHWHEEL_MODE_VELOCITY;
    } else if (strcmp(mode_str, "lfo_rate") == 0) {
      mode = TOUCHWHEEL_MODE_LFO_RATE;
    }
  }

  cJSON_Delete(root);
  return mode;
}

// Read persisted touchwheel output_type from JSON without loading entire scene
output_type_t scene_get_persisted_touchwheel_output_type(uint8_t scene_index) {
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  FILE* f = fopen(filepath, "r");
  if (!f) return OUTPUT_TYPE_CC;  // Default
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return OUTPUT_TYPE_CC; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return OUTPUT_TYPE_CC;
  
  output_type_t output_type = OUTPUT_TYPE_CC;
  cJSON* tw = cJSON_GetObjectItem(root, "touchwheel");
  if (tw) {
    cJSON* ot = cJSON_GetObjectItem(tw, "output_type");
    if (ot && cJSON_IsString(ot)) {
      if (strcmp(ot->valuestring, "note") == 0) {
        output_type = OUTPUT_TYPE_NOTE;
      }
    }
  }
  
  cJSON_Delete(root);
  return output_type;
}

esp_err_t scene_save_to_flash(uint8_t scene_index) {
  scene_t* scene = NULL;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid && g_scene_manager.cache[i].index == scene_index) {
      scene = &g_scene_manager.cache[i].scene;
      break;
    }
  }
  if (!scene) return ESP_ERR_NOT_FOUND;
  
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  
  cJSON* root = scene_to_json(scene);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(filepath, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  ESP_LOGI(TAG, "Saved scene %d to flash", scene_index);
  return ESP_OK;
}

esp_err_t scene_load_manifest(void) {
  FILE* f = fopen(MANIFEST_PATH, "r");
  if (!f) return ESP_ERR_NOT_FOUND;
  
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char* json_str = malloc(fsize + 1);
  if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
  
  fread(json_str, 1, fsize, f);
  fclose(f);
  json_str[fsize] = '\0';
  
  cJSON* root = cJSON_Parse(json_str);
  free(json_str);
  if (!root) return ESP_ERR_INVALID_ARG;
  
  cJSON* scenes = cJSON_GetObjectItem(root, "scenes");
  if (!scenes || !cJSON_IsArray(scenes)) { cJSON_Delete(root); return ESP_ERR_INVALID_ARG; }
  
  int count = cJSON_GetArraySize(scenes);
  g_scene_manager.manifest = malloc_prefer_psram(count * sizeof(scene_manifest_entry_t));
  if (!g_scene_manager.manifest) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
  
  g_scene_manager.num_scenes = count;
  for (int i = 0; i < count; i++) {
    cJSON* entry = cJSON_GetArrayItem(scenes, i);
    cJSON* idx = cJSON_GetObjectItem(entry, "index");
    cJSON* name = cJSON_GetObjectItem(entry, "name");
    cJSON* filename = cJSON_GetObjectItem(entry, "filename");
    
    if (idx) g_scene_manager.manifest[i].index = idx->valueint;
    if (name && cJSON_IsString(name)) {
      strncpy(g_scene_manager.manifest[i].name, name->valuestring,
        sizeof(g_scene_manager.manifest[i].name) - 1);
      g_scene_manager.manifest[i].name[sizeof(g_scene_manager.manifest[i].name) - 1] = '\0';
    }
    if (filename && cJSON_IsString(filename)) {
      strncpy(g_scene_manager.manifest[i].filename, filename->valuestring, 63);
      g_scene_manager.manifest[i].filename[63] = '\0';
    }
  }
  
  cJSON_Delete(root);
  return ESP_OK;
}

esp_err_t scene_save_manifest(void) {
  cJSON* root = cJSON_CreateObject();
  cJSON* scenes = cJSON_CreateArray();
  
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(entry, "index", g_scene_manager.manifest[i].index);
    cJSON_AddStringToObject(entry, "name", g_scene_manager.manifest[i].name);
    cJSON_AddStringToObject(entry, "filename", g_scene_manager.manifest[i].filename);
    cJSON_AddItemToArray(scenes, entry);
  }
  
  cJSON_AddItemToObject(root, "scenes", scenes);
  char* json_str = cJSON_Print(root);
  cJSON_Delete(root);
  if (!json_str) return ESP_ERR_NO_MEM;
  
  FILE* f = fopen(MANIFEST_PATH, "w");
  if (!f) { free(json_str); return ESP_ERR_NOT_FOUND; }
  
  fwrite(json_str, 1, strlen(json_str), f);
  fclose(f);
  free(json_str);
  
  return ESP_OK;
}

uint16_t scene_get_count(void) {
  return g_scene_manager.num_scenes;
}

const char* scene_get_name_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return NULL;
  }
  return g_scene_manager.manifest[position].name;
}

uint8_t scene_get_index_by_position(uint16_t position) {
  if (position >= g_scene_manager.num_scenes || !g_scene_manager.manifest) {
    return 0;
  }
  return g_scene_manager.manifest[position].index;
}

esp_err_t scene_create_new(const char* name) {
  return scene_create_new_at_position(name, g_scene_manager.num_scenes);
}

esp_err_t scene_create_new_at_position(const char* name, uint16_t position) {
  // Find next available index
  uint8_t new_index = 0;
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    bool exists = false;
    for (int j = 0; j < g_scene_manager.num_scenes; j++) {
      if (g_scene_manager.manifest[j].index == i) { exists = true; break; }
    }
    if (!exists) { new_index = i; break; }
  }
  
  // Expand manifest (use PSRAM for persistent data)
  scene_manifest_entry_t* new_manifest = realloc_prefer_psram(g_scene_manager.manifest, 
                                                               (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
  if (!new_manifest) return ESP_ERR_NO_MEM;
  g_scene_manager.manifest = new_manifest;
  
  // Clamp position to valid range
  if (position > g_scene_manager.num_scenes) position = g_scene_manager.num_scenes;
  
  // Shift entries down to make room at position
  for (int i = g_scene_manager.num_scenes; i > (int)position; i--) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }
  
  // Insert new entry at position
  g_scene_manager.manifest[position].index = new_index;
  strncpy(g_scene_manager.manifest[position].name, name,
    sizeof(g_scene_manager.manifest[position].name) - 1);
  g_scene_manager.manifest[position].name[sizeof(g_scene_manager.manifest[position].name) - 1] = '\0';
  snprintf(g_scene_manager.manifest[position].filename, 63, "scene_%03d.json", new_index + 1);
  g_scene_manager.num_scenes++;
  
  // Create and save default scene - initialize directly in cache to avoid stack allocation
  int temp_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;
  scene_init_defaults(&g_scene_manager.cache[temp_idx].scene, new_index);
  strncpy(g_scene_manager.cache[temp_idx].scene.name, name,
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1);
  g_scene_manager.cache[temp_idx].scene.name[sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1] = '\0';
  g_scene_manager.cache[temp_idx].index = new_index;
  g_scene_manager.cache[temp_idx].valid = true;
  
  scene_save_to_flash(new_index);
  scene_save_manifest();
  
  return ESP_OK;
}

esp_err_t scene_delete(uint8_t scene_index) {
  if (g_scene_manager.num_scenes == 1) return ESP_ERR_INVALID_STATE;
  
  int pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == scene_index) { pos = i; break; }
  }
  if (pos == -1) return ESP_ERR_NOT_FOUND;
  
  char filepath[128];
  get_scene_filename(scene_index, filepath, sizeof(filepath));
  remove(filepath);
  
  for (int i = pos; i < g_scene_manager.num_scenes - 1; i++) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  }
  g_scene_manager.num_scenes--;
  
  scene_save_manifest();
  return ESP_OK;
}

esp_err_t scene_duplicate(uint8_t source_index, const char* new_name) {
  // Find source in manifest
  int source_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == source_index) { source_pos = i; break; }
  }
  if (source_pos == -1) return ESP_ERR_NOT_FOUND;

  // Find next available index
  uint8_t new_index = 0;
  for (uint8_t i = 0; i <= MAX_SCENE_INDEX; i++) {
    bool exists = false;
    for (int j = 0; j < g_scene_manager.num_scenes; j++) {
      if (g_scene_manager.manifest[j].index == i) { exists = true; break; }
    }
    if (!exists) { new_index = i; break; }
  }

  // Expand manifest
  scene_manifest_entry_t* new_manifest = realloc_prefer_psram(g_scene_manager.manifest,
    (g_scene_manager.num_scenes + 1) * sizeof(scene_manifest_entry_t));
  if (!new_manifest) return ESP_ERR_NO_MEM;
  g_scene_manager.manifest = new_manifest;

  // Insert right after source
  uint16_t insert_pos = source_pos + 1;
  for (int i = g_scene_manager.num_scenes; i > (int)insert_pos; i--) {
    g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }

  g_scene_manager.manifest[insert_pos].index = new_index;
  strncpy(g_scene_manager.manifest[insert_pos].name, new_name,
    sizeof(g_scene_manager.manifest[insert_pos].name) - 1);
  g_scene_manager.manifest[insert_pos].name[
    sizeof(g_scene_manager.manifest[insert_pos].name) - 1] = '\0';
  snprintf(g_scene_manager.manifest[insert_pos].filename, 63,
    "scene_%03d.json", new_index + 1);
  g_scene_manager.num_scenes++;

  // Use a temp cache slot for the new scene
  int temp_idx = (g_scene_manager.current_cache_idx + 1) % SCENE_CACHE_SIZE;

  // Try to copy source from cache first
  bool copied = false;
  for (int i = 0; i < SCENE_CACHE_SIZE; i++) {
    if (g_scene_manager.cache[i].valid &&
      g_scene_manager.cache[i].index == source_index) {
      g_scene_manager.cache[temp_idx].scene = g_scene_manager.cache[i].scene;
      copied = true;
      break;
    }
  }

  // If not cached, load source from flash into temp slot
  if (!copied) {
    char filepath[128];
    get_scene_filename(source_index, filepath, sizeof(filepath));
    FILE* f = fopen(filepath, "r");
    if (!f) {
      // Rollback manifest change
      for (int i = (int)insert_pos; i < g_scene_manager.num_scenes - 1; i++) {
        g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
      }
      g_scene_manager.num_scenes--;
      return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* json_str = malloc(fsize + 1);
    if (!json_str) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[fsize] = '\0';
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return ESP_ERR_INVALID_ARG;
    scene_init_defaults(&g_scene_manager.cache[temp_idx].scene, new_index);
    json_to_scene(root, &g_scene_manager.cache[temp_idx].scene);
    cJSON_Delete(root);
  }

  // Update the copy with new identity
  strncpy(g_scene_manager.cache[temp_idx].scene.name, new_name,
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1);
  g_scene_manager.cache[temp_idx].scene.name[
    sizeof(g_scene_manager.cache[temp_idx].scene.name) - 1] = '\0';
  g_scene_manager.cache[temp_idx].index = new_index;
  g_scene_manager.cache[temp_idx].valid = true;

  scene_save_to_flash(new_index);
  scene_save_manifest();

  return ESP_OK;
}

esp_err_t scene_reorder(uint8_t from_index, uint8_t to_index) {
  int from_pos = -1, to_pos = -1;
  for (int i = 0; i < g_scene_manager.num_scenes; i++) {
    if (g_scene_manager.manifest[i].index == from_index) from_pos = i;
    if (g_scene_manager.manifest[i].index == to_index) to_pos = i;
  }
  if (from_pos == -1 || to_pos == -1) return ESP_ERR_INVALID_ARG;
  
  scene_manifest_entry_t temp = g_scene_manager.manifest[from_pos];
  
  if (from_pos < to_pos) {
    for (int i = from_pos; i < to_pos; i++) g_scene_manager.manifest[i] = g_scene_manager.manifest[i + 1];
  } else {
    for (int i = from_pos; i > to_pos; i--) g_scene_manager.manifest[i] = g_scene_manager.manifest[i - 1];
  }
  
  g_scene_manager.manifest[to_pos] = temp;
  scene_save_manifest();
  
  return ESP_OK;
}

// ============================================================================
// Input suspension for programming mode
// ============================================================================

// Track LFO running state before entering programming mode
static bool s_lfo_was_running[2] = {false, false};

esp_err_t scene_suspend_input(void) {
  if (s_input_suspended) return ESP_OK;  // Already suspended

  ESP_LOGD(TAG, "Suspending scene input (entering programming mode)");

  // Clean up any active notes before suspending
  touchwheel_cleanup_active_notes();

  // Unregister scene touchwheel so it doesn't receive input
  if (s_scene_touchwheel) {
    touch_unregister_touchwheel_instance(s_scene_touchwheel);
    ESP_LOGD(TAG, "Scene touchwheel unregistered");
  }

  // Mute MIDI clock output in programming mode
  tempo_set_clock_muted(true);

  // Save LFO running state, then stop them to prevent MIDI CC output
  s_lfo_was_running[0] = lfo_is_enabled(0);
  s_lfo_was_running[1] = lfo_is_enabled(1);
  lfo_enable(0, false);
  lfo_enable(1, false);

  s_input_suspended = true;
  return ESP_OK;
}

esp_err_t scene_resume_input(void) {
  if (!s_input_suspended) return ESP_OK;  // Not suspended
  
  ESP_LOGD(TAG, "Resuming scene input (leaving programming mode)");
  
  // Re-register scene touchwheel if it exists
  if (s_scene_touchwheel) {
    touch_register_touchwheel_instance(s_scene_touchwheel);
    ESP_LOGD(TAG, "Scene touchwheel re-registered");
  }
  
  // Unmute MIDI clock output
  tempo_set_clock_muted(false);
  
  // Restore LFO running state (only if no scene change happened;
  // if a scene change happened, scene_apply_deferred_init will handle LFOs)
  if (!s_needs_deferred_init) {
    if (s_lfo_was_running[0]) lfo_enable(0, true);
    if (s_lfo_was_running[1]) lfo_enable(1, true);
  }
  
  s_input_suspended = false;
  return ESP_OK;
}

bool scene_is_input_suspended(void) {
  return s_input_suspended;
}
