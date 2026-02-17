#include "action.h"
#include "midi_messages.h"
#include "midi_lfo_scene_handler.h"
#include "midi_out.h"
#include "device_config.h"
#include "scene.h"
#include "touchwheel_mode_mapping.h"
#include "transport.h"
#include "tempo.h"
#include "assets_manager.h"
#include "lfo.h"
#include "event_bus.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "action";

static bool s_initialized = false;

// ============================================================================
// Clock Burst Timer (for sending extra clock pulses)
// ============================================================================

static esp_timer_handle_t s_clock_burst_timer = NULL;
static uint8_t s_clock_burst_speed_percent = 100;
static bool s_clock_burst_active = false;

static void clock_burst_timer_callback(void* arg) {
  (void)arg;
  if (s_clock_burst_active) {
    // Send an extra clock pulse (0xF8) directly
    // Note: This bypasses the scene's send_clock check intentionally
    // because the burst is a deliberate performance effect
    const uint8_t message = 0xF8;
    midi_send_message(&message, 1);
  }
}

static void clock_burst_start(uint8_t speed_percent) {
  if (s_clock_burst_active) return;  // Already running
  
  // Timer must exist to start burst
  if (!s_clock_burst_timer) {
    ESP_LOGW(TAG, "Clock Burst timer not initialized");
    return;
  }
  
  // Get current BPM from tempo
  uint16_t current_bpm = tempo_get_bpm();
  
  // Calculate the base tick interval (for 24 PPQN)
  // Base ticks per second = (bpm * 24) / 60 = bpm * 0.4
  // Base tick interval = 60000000 / (bpm * 24) microseconds
  // For the burst, we need to add (speed_percent / 100) of that rate
  // So burst interval = base_interval * 100 / speed_percent
  
  // If speed_percent is 100%, we match the existing tempo (double the clocks)
  // If speed_percent is 200%, we send twice as many extra clocks (triple total)
  // If speed_percent is 50%, we send half as many extra clocks (1.5x total)
  
  uint64_t base_interval_us = (60 * 1000000ULL) / (current_bpm * 24);
  uint64_t burst_interval_us = (base_interval_us * 100) / speed_percent;
  
  // Minimum interval of 1ms to avoid overloading
  if (burst_interval_us < 1000) burst_interval_us = 1000;
  
  s_clock_burst_speed_percent = speed_percent;
  
  esp_err_t ret = esp_timer_start_periodic(s_clock_burst_timer, burst_interval_us);
  if (ret == ESP_OK) {
    s_clock_burst_active = true;
    ESP_LOGI(TAG, "Clock Burst started: %d%% speed, interval %llu us",
      speed_percent, (unsigned long long)burst_interval_us);
  } else {
    ESP_LOGE(TAG, "Failed to start Clock Burst timer: %s", esp_err_to_name(ret));
  }
}

static void clock_burst_stop(void) {
  if (!s_clock_burst_active) return;
  
  s_clock_burst_active = false;
  if (s_clock_burst_timer) {
    esp_timer_stop(s_clock_burst_timer);
    ESP_LOGI(TAG, "Clock Burst stopped");
  }
}

// ============================================================================
// Pending Action Queue (for delayed trigger timing)
// ============================================================================

#define MAX_PENDING_ACTIONS 4

typedef struct {
  action_t action;
  action_t* original;         // Pointer to original action for state sync
  uint8_t trigger_value;
  bool valid;
  
  // Phase 1: Trigger timing
  uint8_t target_beat;        // 0 = any beat, 1-16 = specific beat
  
  // Phase 2: Repeating
  bool repeating;             // True if this action should re-queue after firing
  uint16_t beats_remaining;   // Beats until next fire (for multi-bar divisions)
  bool hold_released;         // For HOLD actions: true when user released (stop after current fires)
  
  // Phase 4: Step patterns
  uint8_t pattern_step;       // Current position in pattern (0 to length-1)
} pending_action_t;

static pending_action_t s_pending_actions[MAX_PENDING_ACTIONS];

// ============================================================================
// Active Repeating Actions Tracking (for toggle behavior)
// ============================================================================

#define MAX_ACTIVE_REPEATS 4
static action_t* s_active_repeating[MAX_ACTIVE_REPEATS];

// ============================================================================
// Morph (interpolation) System
// ============================================================================

#define MAX_ACTIVE_MORPHS 4

// Max discrete values to track per CC (keep small for memory)
#define MORPH_MAX_DISCRETE 8

typedef struct {
  bool active;
  action_t* action;           // For state sync (cycle index)
  uint8_t num_ccs;
  uint8_t cc_numbers[4];
  uint8_t start_values[4];    // Values at morph start
  uint8_t target_values[4];   // Values at morph end
  uint8_t current_step;       // 0 to total_steps-1
  uint8_t total_steps;
  uint32_t step_interval_ms;  // Time between steps
  uint32_t next_step_time;    // When to send next value (ms timestamp)
  
  // For SYNC mode: target moment tracking
  morph_timing_mode_t timing_mode;
  uint8_t sync_target_beat;   // Target beat (1-16, 0=any/bar start)
  bool sync_waiting_final;    // True when waiting for beat event to send final value
  
  // Per-CC control metadata (looked up at morph start)
  uint8_t last_sent_values[4];           // Avoid redundant sends
  uint8_t discrete_counts[4];            // 0 = continuous, 1+ = discrete
  uint8_t discrete_values[4][MORPH_MAX_DISCRETE];  // Discrete values per CC
  bool delay_final[4];                   // True for low-count discrete CCs (≤4 values)
} active_morph_t;

static active_morph_t s_active_morphs[MAX_ACTIVE_MORPHS];
static esp_timer_handle_t s_morph_timer = NULL;
static uint8_t s_last_cc_values[128];  // Track last sent CC values for morph start

// Forward declarations for morph functions
static void morph_timer_callback(void* arg);
static void morph_beat_event_handler(const event_t* event, void* context);
static void morph_advance_step(active_morph_t* m);
static void morph_send_final_values(active_morph_t* m);
static uint8_t calculate_auto_steps(uint8_t value_delta, uint32_t duration_ms);
static uint32_t get_feel_duration_ms(morph_feel_t feel, uint16_t bpm);
static uint32_t get_duration_ms(morph_division_t div, uint16_t bpm);
static uint32_t get_sync_duration_ms(morph_division_t div, uint16_t bpm,
  uint8_t current_beat, uint8_t beats_per_bar);
static bool morph_start(const action_t* action, uint8_t num_ccs,
  const uint8_t* cc_numbers, const uint8_t* target_values);
static void morph_update_timer(void);
static int find_discrete_index(const uint8_t* values, uint8_t count, uint8_t target);

// Check if an action is currently repeating
static bool is_action_repeating(action_t* action) {
  if (!action) return false;
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == action) return true;
  }
  return false;
}

// Mark an action as actively repeating
static bool start_repeating(action_t* action) {
  if (!action) return false;
  // Check if already repeating
  if (is_action_repeating(action)) return true;
  // Find empty slot
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == NULL) {
      s_active_repeating[i] = action;
      ESP_LOGD(TAG, "Started repeating action %s", action_type_to_string(action->type));
      return true;
    }
  }
  ESP_LOGW(TAG, "No slot available for repeating action");
  return false;
}

// Stop repeating an action
static void stop_repeating_internal(action_t* action) {
  if (!action) return;
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    if (s_active_repeating[i] == action) {
      s_active_repeating[i] = NULL;
      ESP_LOGD(TAG, "Stopped repeating action %s", action_type_to_string(action->type));
      
      // Also clear any pending instances of this action
      for (int j = 0; j < MAX_PENDING_ACTIONS; j++) {
        if (s_pending_actions[j].valid && s_pending_actions[j].original == action) {
          s_pending_actions[j].valid = false;
        }
      }
      return;
    }
  }
}

// Public function to stop repeating (called from outside)
void action_stop_repeating(action_t* action) {
  stop_repeating_internal(action);
}

// Clear all active repeating actions
static void clear_all_repeating(void) {
  for (int i = 0; i < MAX_ACTIVE_REPEATS; i++) {
    s_active_repeating[i] = NULL;
  }
}

// Forward declaration for internal execute (bypasses timing check)
static esp_err_t action_execute_immediate(const action_t* action, uint8_t trigger_value, bool is_press);

// Beat event handler - fires pending actions when beat matches
static void handle_beat_event(const event_t* event, void* context) {
  if (event->type != EVENT_BEAT) return;
  
  uint8_t current_beat = event->data.beat.beat_in_bar;
  uint8_t beats_per_bar = event->data.beat.bar_length;
  if (beats_per_bar == 0) beats_per_bar = 4;
  
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) continue;
    
    pending_action_t* pending = &s_pending_actions[i];
    bool should_fire = false;
    
    // For repeating actions with multi-bar divisions, decrement beats_remaining
    if (pending->repeating && pending->beats_remaining > 1) {
      pending->beats_remaining--;
      continue;  // Not time to fire yet
    }
    
    if (pending->target_beat == 0) {
      // Any beat triggers
      should_fire = true;
    } else if (pending->target_beat == current_beat) {
      // Specific beat matches
      should_fire = true;
    }
    
    if (should_fire) {
      // For repeating actions, check pattern first (before probability)
      bool pattern_passed = true;
      if (pending->repeating && pending->action.pattern_length >= 2) {
        // Check if current step is active in the pattern mask
        pattern_passed = (pending->action.pattern_mask >> pending->pattern_step) & 1;
        if (!pattern_passed) {
          ESP_LOGD(TAG, "Pattern step %d skipped for %s",
            pending->pattern_step + 1, action_type_to_string(pending->action.type));
        }
        // Advance to next step (wrap around)
        pending->pattern_step = (pending->pattern_step + 1) % pending->action.pattern_length;
      }
      
      // For repeating actions, check probability (default 100 = always fire)
      bool probability_passed = true;
      if (pattern_passed && pending->repeating) {
        uint8_t prob = pending->action.probability;
        if (prob == 0) prob = 100;  // Default to 100% if not set
        if (prob < 100) {
          // Roll 0-99, pass if roll < probability
          uint8_t roll = (uint8_t)(esp_random() % 100);
          probability_passed = (roll < prob);
          if (!probability_passed) {
            ESP_LOGD(TAG, "Probability check failed (%d%%, rolled %d) for %s",
              prob, roll, action_type_to_string(pending->action.type));
          }
        }
      }
      
      if (pattern_passed && probability_passed) {
        ESP_LOGD(TAG, "Firing pending action %s on beat %d",
          action_type_to_string(pending->action.type), current_beat);
        
        // Execute immediately (press only - releases are never queued)
        action_execute_immediate(&pending->action, pending->trigger_value, true);
        
        // Sync cycle state back to original action (for CYCLE actions)
        if (pending->original) {
          switch (pending->action.type) {
            case ACTION_CONTROL_CYCLE:
              pending->original->params.control.current_index =
                pending->action.params.control.current_index;
              break;
            case ACTION_PRESET_CYCLE:
              pending->original->params.preset_cycle.current_index =
                pending->action.params.preset_cycle.current_index;
              break;
            case ACTION_TEMPO_CYCLE:
              pending->original->params.tempo.current_index =
                pending->action.params.tempo.current_index;
              break;
            case ACTION_TOUCHWHEEL_CYCLE:
              pending->original->params.tw_mode.current_index =
                pending->action.params.tw_mode.current_index;
              break;
            case ACTION_LFO_SHAPE:
              pending->original->params.lfo.current_index =
                pending->action.params.lfo.current_index;
              break;
            default:
              break;
          }
        }
      }
      
      // Handle repeating actions - re-queue for next interval (even if pattern/probability failed)
      if (pending->repeating && !pending->hold_released) {
        // Calculate beats until next fire based on division
        uint8_t interval_beats = action_repeat_division_to_beats(
          pending->action.repeat_division, beats_per_bar);
        
        if (interval_beats > 0) {
          // Multi-beat or multi-bar division
          pending->beats_remaining = interval_beats;
          // Keep same target_beat for bar-aligned repeats, or 0 for any-beat
          ESP_LOGD(TAG, "Re-queued repeating action for %d beats", interval_beats);
        } else {
          // Sub-beat divisions (eighth, sixteenth, 32nd) - not yet supported
          // For now, treat as every beat
          pending->beats_remaining = 1;
        }
        // Keep slot valid for next fire
      } else {
        // Not repeating, or HOLD was released - clear slot
        pending->valid = false;
        if (pending->original) {
          stop_repeating_internal(pending->original);
        }
      }
    }
  }
}

// Enqueue an action for delayed execution
static bool action_enqueue_pending(action_t* action, uint8_t trigger_value, uint8_t target_beat, bool repeating) {
  // Find empty slot
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) {
      s_pending_actions[i].action = *action;
      s_pending_actions[i].original = action;  // Store pointer to original for state sync
      s_pending_actions[i].trigger_value = trigger_value;
      s_pending_actions[i].target_beat = target_beat;
      s_pending_actions[i].valid = true;
      s_pending_actions[i].repeating = repeating;
      s_pending_actions[i].beats_remaining = 1;  // Fire on first matching beat
      s_pending_actions[i].hold_released = false;
      s_pending_actions[i].pattern_step = 0;     // Start at first step in pattern
      
      ESP_LOGD(TAG, "Queued action %s for beat %d (slot %d, repeating=%d)",
        action_type_to_string(action->type),
        target_beat == 0 ? -1 : target_beat, i, repeating);
      return true;
    }
  }
  
  ESP_LOGW(TAG, "Pending action queue full, executing immediately");
  return false;
}

void action_clear_pending(void) {
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    s_pending_actions[i].valid = false;
  }
  clear_all_repeating();
  ESP_LOGD(TAG, "Cleared pending action queue and repeating actions");
}

// Action type names for debugging
static const char* action_type_names[] = {
  [ACTION_NONE] = "None",
  [ACTION_PRESET_INC] = "Preset +1",
  [ACTION_PRESET_DEC] = "Preset -1",
  [ACTION_PRESET] = "Set Preset",
  [ACTION_PRESET_HOLD] = "Preset Hold",
  [ACTION_PRESET_CYCLE] = "Preset Cycle",
  [ACTION_SCENE_INC] = "Scene +1",
  [ACTION_SCENE_DEC] = "Scene -1",
  [ACTION_SCENE] = "Set Scene",
  [ACTION_PLAY] = "Play",
  [ACTION_STOP] = "Stop",
  [ACTION_PAUSE] = "Pause",
  [ACTION_RECORD] = "Record",
  [ACTION_TAP] = "Tap",
  [ACTION_TAP_TEMPO] = "Tap Tempo",
  [ACTION_SET_TEMPO] = "Set Tempo",
  [ACTION_TEMPO_INC] = "Tempo +1",
  [ACTION_TEMPO_DEC] = "Tempo -1",
  [ACTION_TEMPO_HOLD] = "Tempo Hold",
  [ACTION_TEMPO_CYCLE] = "Tempo Cycle",
  [ACTION_CONTROL_CHANGE] = "Control Change",
  [ACTION_CONTROL_HOLD] = "Control Hold",
  [ACTION_CONTROL_CYCLE] = "Control Cycle",
  [ACTION_NOTE] = "Note",
  [ACTION_RANDOMIZE] = "Randomize",
  [ACTION_CONFIRM_PENDING] = "Confirm Pending",
  [ACTION_RESET] = "Reset",
  [ACTION_SUSTAIN] = "Sustain",
  [ACTION_SOSTENUTO] = "Sostenuto",
  [ACTION_TOUCHWHEEL_HOLD] = "Touchwheel Hold",
  [ACTION_TOUCHWHEEL_CYCLE] = "Touchwheel Cycle",
  [ACTION_LFO_START] = "LFO Start",
  [ACTION_LFO_STOP] = "LFO Stop",
  [ACTION_LFO_TOGGLE] = "LFO Toggle",
  [ACTION_LFO_SHAPE] = "LFO Shape",
  [ACTION_CLOCK_TOGGLE] = "Clock Toggle",
  [ACTION_CLOCK_HOLD] = "Clock Hold",
  [ACTION_CLOCK_BURST] = "Clock Burst",
  [ACTION_CUT_TOGGLE] = "Cut Toggle",
  [ACTION_CUT_HOLD] = "Cut Hold",
  [ACTION_SET_UI] = "Set UI",
  [ACTION_UI_HOLD] = "UI Hold",
  [ACTION_UI_CYCLE] = "UI Cycle",
  [ACTION_PARAM_HOLD] = "Param Hold",
  [ACTION_PARAM_CYCLE] = "Param Cycle"
};

esp_err_t action_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Action system already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing action system");
  
  // Initialize pending action queue
  action_clear_pending();
  
  // Subscribe to beat events for delayed action triggering
  esp_err_t ret = event_bus_subscribe(EVENT_BEAT, handle_beat_event, NULL);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to beat events: %s", esp_err_to_name(ret));
  }
  
  // Create clock burst timer
  esp_timer_create_args_t timer_args = {
    .callback = clock_burst_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "clock_burst"
  };
  ret = esp_timer_create(&timer_args, &s_clock_burst_timer);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create clock burst timer: %s", esp_err_to_name(ret));
  }
  
  // Initialize morph system
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    s_active_morphs[i].active = false;
  }
  memset(s_last_cc_values, 64, sizeof(s_last_cc_values));  // Default to center
  
  // Create morph timer (periodic, checks for morphs needing advancement)
  esp_timer_create_args_t morph_timer_args = {
    .callback = morph_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "morph"
  };
  ret = esp_timer_create(&morph_timer_args, &s_morph_timer);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to create morph timer: %s", esp_err_to_name(ret));
  }
  
  // Subscribe to beat events for SYNC mode morph completion
  ret = event_bus_subscribe(EVENT_BEAT, morph_beat_event_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Failed to subscribe to beat events for morph: %s", esp_err_to_name(ret));
  }
  
  s_initialized = true;
  
  return ESP_OK;
}

const char* action_type_to_string(action_type_t type) {
  if (type >= ACTION_MAX) return "Unknown";
  const char* name = action_type_names[type];
  return name ? name : "Unknown";
}

// Public action_execute - handles timing, may queue for delayed execution
esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press) {
  if (!action || action->type == ACTION_NONE) {
    return ESP_OK;
  }
  
  action_t* mutable_action = (action_t*)action;  // Cast away const for state tracking
  
  // Handle HOLD action release for repeating actions - mark hold_released
  if (!is_press && action_requires_hold(action->type) && action->repeat_enabled) {
    // Find pending action and mark as released
    for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
      if (s_pending_actions[i].valid && s_pending_actions[i].original == mutable_action) {
        s_pending_actions[i].hold_released = true;
        ESP_LOGD(TAG, "HOLD action released, will stop after pending fire");
        break;
      }
    }
    // Still execute the release immediately for HOLD actions
    return action_execute_immediate(action, trigger_value, is_press);
  }
  
  // Handle non-HOLD repeating actions - toggle behavior
  if (is_press && action->repeat_enabled && action_supports_repeat(action->type)) {
    if (is_action_repeating(mutable_action)) {
      // Currently repeating - stop it
      stop_repeating_internal(mutable_action);
      ESP_LOGD(TAG, "Stopped repeating action (toggle off)");
      return ESP_OK;  // Don't fire again
    } else {
      // Not currently repeating - start it
      start_repeating(mutable_action);
    }
  }
  
  // Releases always execute immediately (to complete press/release pairs)
  // HOLD actions always execute immediately (need press/release pairing)
  // IMMEDIATE timing executes immediately
  bool should_queue = is_press &&
                      action_supports_timing(action->type) &&
                      action->timing != ACTION_TIMING_IMMEDIATE;
  
  if (should_queue) {
    uint8_t target_beat;
    if (action->timing == ACTION_TIMING_NEXT_BEAT) {
      target_beat = 0;  // 0 = any beat
    } else {
      target_beat = action->timing_beat;  // Specific beat 1-16
    }
    
    // Determine if this is a repeating action
    bool repeating = action->repeat_enabled && action_supports_repeat(action->type);
    
    if (action_enqueue_pending(mutable_action, trigger_value, target_beat, repeating)) {
      return ESP_OK;  // Successfully queued
    }
    // Fall through to immediate execution if queue is full
  }
  
  return action_execute_immediate(action, trigger_value, is_press);
}

// Internal immediate execute - bypasses timing check
static esp_err_t action_execute_immediate(const action_t* action, uint8_t trigger_value, bool is_press) {
  if (!action || action->type == ACTION_NONE) {
    return ESP_OK;
  }
  
  ESP_LOGD(TAG, "Executing action: %s (trigger=%d, press=%d)", 
           action_type_to_string(action->type), trigger_value, is_press);
  
  uint8_t channel = device_config_get_channel() - 1;  // MIDI uses 0-based channels
  
  // Get current scene mode for action validation
  scene_mode_t current_mode = scene_get_mode();
  
  switch (action->type) {
    // Preset control - disabled in Preset Sync mode (preset locked to scene ordinal)
    case ACTION_PRESET_INC:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset +1 action ignored: not allowed in Preset Sync mode");
        break;
      }
      if (is_press) device_config_program_next();
      break;
      
    case ACTION_PRESET_DEC:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset -1 action ignored: not allowed in Preset Sync mode");
        break;
      }
      if (is_press) device_config_program_prev();
      break;
      
    case ACTION_PRESET:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Set Preset action ignored: not allowed in Preset Sync mode");
        break;
      }
      if (is_press) {
        // Smart PC: uses bank mode setting to decide behavior
        uint16_t program = action->params.preset.program;
        if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
          // Bank mode: treat as preset 0-16383
          device_config_set_preset(program);
        } else {
          // No bank: treat as program 0-127
          device_config_set_program(program & 0x7F);
        }
      }
      break;
      
    case ACTION_PRESET_HOLD:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset Hold action ignored: not allowed in Preset Sync mode");
        break;
      }
      // Set press_preset on press, release_preset on release
      {
        uint16_t program = is_press ? 
          action->params.preset_cycle.press_preset : 
          action->params.preset_cycle.release_preset;
        if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
          device_config_set_preset(program);
        } else {
          device_config_set_program(program & 0x7F);
        }
        ESP_LOGD(TAG, "Preset hold: %s -> %u", is_press ? "press" : "release", 
          (unsigned)program);
      }
      break;
      
    case ACTION_PRESET_CYCLE:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset Cycle action ignored: not allowed in Preset Sync mode");
        break;
      }
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_presets = mutable_action->params.preset_cycle.num_presets;
        if (num_presets == 0) {
          ESP_LOGW(TAG, "Preset cycle has no presets defined, skipping");
          break;
        }
        uint8_t idx = mutable_action->params.preset_cycle.current_index;
        uint16_t program = mutable_action->params.preset_cycle.cycle_presets[idx];
        
        if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
          device_config_set_preset(program);
        } else {
          device_config_set_program(program & 0x7F);
        }
        ESP_LOGD(TAG, "Preset cycle step %u: preset %u", (unsigned)idx, (unsigned)program);
        
        // Advance to next step
        mutable_action->params.preset_cycle.current_index = (idx + 1) % num_presets;
      }
      break;
      
    // Scene control - disabled in Simple mode (only one scene exists)
    case ACTION_SCENE_INC:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Scene +1 action ignored: not allowed in Simple mode");
        break;
      }
      if (is_press) scene_next();
      break;
      
    case ACTION_SCENE_DEC:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Scene -1 action ignored: not allowed in Simple mode");
        break;
      }
      if (is_press) scene_previous();
      break;
      
    case ACTION_SCENE:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Set Scene action ignored: not allowed in Simple mode");
        break;
      }
      // Scene numbers are 1-based for users, 0-based internally
      if (is_press && action->params.target.number >= 1) {
        scene_set_current(action->params.target.number - 1);
      }
      break;
      
    // Transport
    case ACTION_PLAY:
      if (is_press) transport_play();
      break;
      
    case ACTION_STOP:
      if (is_press) transport_stop();
      break;
      
    case ACTION_PAUSE:
      if (is_press) transport_pause();
      break;
      
    case ACTION_RECORD:
      if (is_press) transport_record();
      break;
      
    // Tempo
    case ACTION_TAP:
      if (is_press) tempo_tap();
      break;
      
    case ACTION_TAP_TEMPO:
      // Toggle tap tempo session based on mode
      if (is_press) {
        tap_tempo_mode_t mode = tempo_get_tap_mode();
        if (mode == TAP_MODE_HOLD) {
          tempo_tap_session_start();
        } else {
          // Toggle or Time mode - toggle on press
          tempo_tap_session_toggle();
        }
      } else {
        // Release - only matters for HOLD mode
        if (tempo_get_tap_mode() == TAP_MODE_HOLD) {
          tempo_tap_session_stop();
        }
      }
      break;
      
    case ACTION_SET_TEMPO:
      if (is_press && action->params.tempo.bpm > 0) {
        tempo_set_bpm(action->params.tempo.bpm);
      }
      break;
      
    case ACTION_TEMPO_INC:
      if (is_press) {
        uint16_t bpm = tempo_get_bpm();
        if (bpm < 300) tempo_set_bpm(bpm + 1);
      }
      break;
      
    case ACTION_TEMPO_DEC:
      if (is_press) {
        uint16_t bpm = tempo_get_bpm();
        if (bpm > 20) tempo_set_bpm(bpm - 1);
      }
      break;
      
    case ACTION_TEMPO_HOLD:
      // Set press_bpm on press, release_bpm on release
      {
        uint16_t bpm = is_press ? 
          action->params.tempo.press_bpm : action->params.tempo.release_bpm;
        if (bpm >= 20 && bpm <= 300) {
          tempo_set_bpm(bpm);
          ESP_LOGD(TAG, "Tempo hold: %s -> %u BPM", is_press ? "press" : "release", 
            (unsigned)bpm);
        }
      }
      break;
      
    case ACTION_TEMPO_CYCLE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_tempos = mutable_action->params.tempo.num_tempos;
        if (num_tempos == 0) {
          ESP_LOGW(TAG, "Tempo cycle has no tempos defined, skipping");
          break;
        }
        uint8_t idx = mutable_action->params.tempo.current_index;
        uint16_t bpm = mutable_action->params.tempo.cycle_tempos[idx];
        
        if (bpm >= 20 && bpm <= 300) {
          tempo_set_bpm(bpm);
          ESP_LOGD(TAG, "Tempo cycle step %u: %u BPM", (unsigned)idx, (unsigned)bpm);
        }
        
        // Advance to next step
        mutable_action->params.tempo.current_index = (idx + 1) % num_tempos;
      }
      break;
      
    // MIDI Control actions (supports multi-CC: 1-4 CCs per action)
    case ACTION_CONTROL_CHANGE:
      // Send CC value(s) on press only (one-shot)
      if (is_press) {
        uint8_t num_ccs = action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;  // Backward compat
        for (int i = 0; i < num_ccs && i < 4; i++) {
          uint8_t cc = action->params.control.cc_numbers[i];
          uint8_t value = action->params.control.values[i];
          send_control_change(channel, cc, value);
          s_last_cc_values[cc] = value;  // Track for morph start values
          ESP_LOGD(TAG, "Sent CC%d=%d", cc, value);
        }
      }
      break;
      
    case ACTION_CONTROL_HOLD:
      // Send value on press, value2 on release (momentary hold behavior)
      {
        uint8_t num_ccs = action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;  // Backward compat
        
        // Determine target values based on press/release
        uint8_t target_values[4];
        for (int i = 0; i < num_ccs && i < 4; i++) {
          target_values[i] = is_press ?
            action->params.control.values[i] : action->params.control.values2[i];
        }
        
        // Use morphing if enabled
        if (action->morph_enabled) {
          if (morph_start(action, num_ccs, action->params.control.cc_numbers, target_values)) {
            ESP_LOGD(TAG, "CC%d hold morph started -> %d", 
              action->params.control.cc_numbers[0], target_values[0]);
            break;  // Morph will handle sending values
          }
          // Fall through to immediate send if morph failed
        }
        
        // Immediate send (no morph)
        for (int i = 0; i < num_ccs && i < 4; i++) {
          send_control_change(channel, action->params.control.cc_numbers[i], target_values[i]);
          s_last_cc_values[action->params.control.cc_numbers[i]] = target_values[i];
          ESP_LOGD(TAG, "CC%d hold: %d", action->params.control.cc_numbers[i], target_values[i]);
        }
      }
      break;
      
    case ACTION_CONTROL_CYCLE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;  // Cast away const for state tracking
        uint8_t num_steps = mutable_action->params.control.num_cycle_steps;
        if (num_steps == 0) {
          ESP_LOGW(TAG, "Control cycle has no steps defined, skipping");
          break;
        }
        uint8_t idx = mutable_action->params.control.current_index;
        uint8_t num_ccs = mutable_action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;  // Backward compat
        
        // Get target values for this cycle step
        uint8_t target_values[4];
        for (int i = 0; i < num_ccs && i < 4; i++) {
          target_values[i] = mutable_action->params.control.cycle_values[i][idx];
        }
        
        // Use morphing if enabled
        if (action->morph_enabled) {
          if (morph_start(action, num_ccs, mutable_action->params.control.cc_numbers, 
              target_values)) {
            ESP_LOGD(TAG, "CC%d cycle morph started -> %d", 
              mutable_action->params.control.cc_numbers[0], target_values[0]);
            // Advance to next step (shared across all CCs)
            mutable_action->params.control.current_index = (idx + 1) % num_steps;
            break;  // Morph will handle sending values
          }
          // Fall through to immediate send if morph failed
        }
        
        // Immediate send (no morph)
        for (int i = 0; i < num_ccs && i < 4; i++) {
          send_control_change(channel, mutable_action->params.control.cc_numbers[i], 
            target_values[i]);
          s_last_cc_values[mutable_action->params.control.cc_numbers[i]] = target_values[i];
          ESP_LOGD(TAG, "Cycled CC%d to %d", mutable_action->params.control.cc_numbers[i], 
            target_values[i]);
        }
        
        // Advance to next step (shared across all CCs)
        mutable_action->params.control.current_index = (idx + 1) % num_steps;
      }
      break;
      
    // Note action (hold-style: press=on, release=off)
    case ACTION_NOTE:
      if (is_press) {
        send_note_on(channel, action->params.note.note, action->params.note.velocity);
        ESP_LOGD(TAG, "Note On: %d vel=%d", action->params.note.note, action->params.note.velocity);
      } else {
        send_note_off(channel, action->params.note.note, 0);
        ESP_LOGD(TAG, "Note Off: %d", action->params.note.note);
      }
      break;
      
    // Randomization
    case ACTION_RANDOMIZE:
      if (is_press) {
        // Get device definition for control constraints
        uint8_t scene_index = scene_get_current_index();
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
        
        uint8_t num_ccs = action->params.randomize.num_ccs;
        if (num_ccs > 8) num_ccs = 8;
        
        // Calculate random target values for all CCs
        uint8_t target_values[8];
        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          uint8_t random_val;
          
          // Look up control definition
          const midi_control_t* ctrl = device ? 
            assets_get_control_by_cc(device, cc) : NULL;
          
          if (ctrl && ctrl->discrete_count > 0 && ctrl->discrete_values) {
            // Has discrete values: pick one randomly
            uint8_t idx = esp_random() % ctrl->discrete_count;
            random_val = ctrl->discrete_values[idx].value;
          } else if (ctrl) {
            // Use min/max range
            uint8_t range = ctrl->max - ctrl->min + 1;
            random_val = ctrl->min + (esp_random() % range);
          } else {
            // Fallback: full 0-127 range
            random_val = esp_random() % 128;
          }
          
          target_values[i] = random_val;
        }
        
        // Use morphing if enabled (limited to first 4 CCs)
        if (action->morph_enabled && num_ccs > 0) {
          uint8_t morph_ccs = (num_ccs > 4) ? 4 : num_ccs;
          if (morph_start(action, morph_ccs, action->params.randomize.cc_numbers, 
              target_values)) {
            ESP_LOGD(TAG, "Randomize morph started for %d CCs", morph_ccs);
            
            // Send remaining CCs (5-8) immediately if any
            for (int i = 4; i < num_ccs; i++) {
              uint8_t cc = action->params.randomize.cc_numbers[i];
              send_control_change(channel, cc, target_values[i]);
              s_last_cc_values[cc] = target_values[i];
              ESP_LOGD(TAG, "Randomized CC%d to %d (immediate)", cc, target_values[i]);
            }
            break;  // Morph will handle first 4 CCs
          }
          // Fall through to immediate send if morph failed
        }
        
        // Immediate send (no morph)
        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          send_control_change(channel, cc, target_values[i]);
          s_last_cc_values[cc] = target_values[i];
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, target_values[i]);
        }
        ESP_LOGD(TAG, "Randomized %d CCs", num_ccs);
      }
      break;
      
    // System
    case ACTION_CONFIRM_PENDING:
      if (is_press) {
        scene_mode_t mode = scene_get_mode();
        if (mode == SCENE_MODE_SINGLE) {
          // Simple mode: only preset changes possible
          if (device_config_has_pending_program()) device_config_confirm_program();
        } else if (mode == SCENE_MODE_PRESET_SYNC) {
          // Preset Sync: only scene changes possible (presets tied to scenes)
          if (scene_has_pending_change()) scene_confirm_change();
        } else {
          // Advanced mode: check action's target setting
          if (action->params.confirm.target == CONFIRM_TARGET_SCENE) {
            if (scene_has_pending_change()) scene_confirm_change();
          } else {
            if (device_config_has_pending_program()) device_config_confirm_program();
          }
        }
      }
      break;
      
    case ACTION_RESET:
      // Combined reset: CC123 (All Notes Off) + CC120 (All Sound Off) + System Reset
      if (is_press) {
        send_control_change(channel, 123, 0);  // All Notes Off
        send_control_change(channel, 120, 0);  // All Sound Off
        send_reset();                          // System Reset (0xFF)
        ESP_LOGD(TAG, "Sent Reset (CC123 + CC120 + 0xFF)");
      }
      break;
      
    case ACTION_SUSTAIN:
      // Send CC64 = 127 on press, 0 on release
      send_control_change(channel, 64, is_press ? 127 : 0);
      ESP_LOGD(TAG, "Sustain: %s", is_press ? "on" : "off");
      break;
      
    case ACTION_SOSTENUTO:
      // Send CC66 = 127 on press, 0 on release
      send_control_change(channel, 66, is_press ? 127 : 0);
      ESP_LOGD(TAG, "Sostenuto: %s", is_press ? "on" : "off");
      break;
      
    // Touchwheel mode control
    case ACTION_TOUCHWHEEL_HOLD:
      // Set mode on press, restore mode2 on release (using user-facing mode indices)
      // Uses runtime function - no persistence (changes are temporary)
      {
        uint8_t user_mode_idx = is_press ? 
          action->params.tw_mode.mode : action->params.tw_mode.mode2;
        const touchwheel_mode_mapping_t* mapping = touchwheel_get_mode_mapping(user_mode_idx);
        if (mapping) {
          uint8_t scene_index = scene_get_current_index();
          scene_t* scene = scene_get_current();
          if (scene) {
            // Apply mode's default style so touchwheel setup uses correct processor
            scene->touchwheel_style = mapping->default_style;
            // Enable touchwheel for all modes except Pads
            scene->touchwheel.enabled = (mapping->mode != TOUCHWHEEL_MODE_PADS);
            if (mapping->use_output_type) {
              scene->touchwheel.output_type = mapping->output_type;
              // For Notes mode, ensure sensible defaults if not configured
              if (mapping->output_type == OUTPUT_TYPE_NOTE) {
                if (scene->touchwheel.base_note == 0) scene->touchwheel.base_note = 60;
                if (scene->touchwheel.note_range == 0) scene->touchwheel.note_range = 24;
                if (scene->touchwheel.velocity == 0) scene->touchwheel.velocity = 100;
              }
            }
          }
          scene_set_touchwheel_mode_runtime(scene_index, mapping->mode);
          ESP_LOGD(TAG, "Touchwheel mode hold: %s", mapping->display_name);
        }
      }
      break;
      
    case ACTION_TOUCHWHEEL_CYCLE:
      // Uses runtime function - no persistence (changes are temporary)
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t idx = mutable_action->params.tw_mode.current_index;
        uint8_t user_mode_idx = mutable_action->params.tw_mode.modes[idx];
        const touchwheel_mode_mapping_t* mapping = touchwheel_get_mode_mapping(user_mode_idx);
        
        if (mapping) {
          uint8_t scene_index = scene_get_current_index();
          scene_t* scene = scene_get_current();
          if (scene) {
            // Apply mode's default style so touchwheel setup uses correct processor
            scene->touchwheel_style = mapping->default_style;
            // Enable touchwheel for all modes except Pads
            scene->touchwheel.enabled = (mapping->mode != TOUCHWHEEL_MODE_PADS);
            if (mapping->use_output_type) {
              scene->touchwheel.output_type = mapping->output_type;
              // For Notes mode, ensure sensible defaults if not configured
              if (mapping->output_type == OUTPUT_TYPE_NOTE) {
                if (scene->touchwheel.base_note == 0) scene->touchwheel.base_note = 60;
                if (scene->touchwheel.note_range == 0) scene->touchwheel.note_range = 24;
                if (scene->touchwheel.velocity == 0) scene->touchwheel.velocity = 100;
              }
            }
          }
          scene_set_touchwheel_mode_runtime(scene_index, mapping->mode);
          ESP_LOGD(TAG, "Cycled touchwheel mode to %s", mapping->display_name);
        }
        
        // Advance to next mode
        mutable_action->params.tw_mode.current_index =
          (idx + 1) % mutable_action->params.tw_mode.num_modes;
      }
      break;

    case ACTION_LFO_START:
      if (is_press) {
        uint8_t slot = action->params.lfo.slot;
        scene_t* scene = scene_get_current();
        if (slot == 1 || slot == 3) {
          lfo_trigger_start(0);
          if (scene) scene->lfo1.enabled = true;
        }
        if (slot == 2 || slot == 3) {
          lfo_trigger_start(1);
          if (scene) scene->lfo2.enabled = true;
        }
        ESP_LOGI(TAG, "LFO Start: slot %d", slot);
      }
      break;

    case ACTION_LFO_STOP:
      if (is_press) {
        uint8_t slot = action->params.lfo.slot;
        scene_t* scene = scene_get_current();
        if (slot == 1 || slot == 3) {
          // Only restore if actually running
          if (lfo_is_enabled(0)) {
            if (lfo_get_restore_on_stop(0)) {
              midi_lfo_scene_handler_restore_value(0);
            }
            lfo_enable(0, false);
            if (scene) scene->lfo1.enabled = false;
          } else if (lfo_is_pending_start(0)) {
            // Cancel pending start
            lfo_enable(0, false);
          }
        }
        if (slot == 2 || slot == 3) {
          if (lfo_is_enabled(1)) {
            if (lfo_get_restore_on_stop(1)) {
              midi_lfo_scene_handler_restore_value(1);
            }
            lfo_enable(1, false);
            if (scene) scene->lfo2.enabled = false;
          } else if (lfo_is_pending_start(1)) {
            lfo_enable(1, false);
          }
        }
        ESP_LOGI(TAG, "LFO Stop: slot %d", slot);
      }
      break;

    case ACTION_LFO_TOGGLE:
      if (is_press) {
        uint8_t slot = action->params.lfo.slot;
        scene_t* scene = scene_get_current();
        if (slot == 1 || slot == 3) {
          bool new_state = !lfo_is_enabled(0);
          // Restore CC value before disabling if configured
          if (!new_state && lfo_get_restore_on_stop(0)) {
            midi_lfo_scene_handler_restore_value(0);
          }
          lfo_enable(0, new_state);
          if (scene) scene->lfo1.enabled = new_state;
        }
        if (slot == 2 || slot == 3) {
          bool new_state = !lfo_is_enabled(1);
          if (!new_state && lfo_get_restore_on_stop(1)) {
            midi_lfo_scene_handler_restore_value(1);
          }
          lfo_enable(1, new_state);
          if (scene) scene->lfo2.enabled = new_state;
        }
        ESP_LOGI(TAG, "LFO Toggle: slot %d", slot);
      }
      break;

    case ACTION_LFO_SHAPE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_shapes = mutable_action->params.lfo.num_shapes;

        // Safety: clamp num_shapes to valid range (2-8)
        if (num_shapes < 2 || num_shapes > 8) {
          ESP_LOGW(TAG, "LFO Shape: num_shapes=%d invalid, skipping", num_shapes);
          break;
        }

        uint8_t idx = mutable_action->params.lfo.current_index;
        // Bounds check against both num_shapes and array size
        if (idx >= num_shapes || idx >= 8) idx = 0;

        uint8_t shape = mutable_action->params.lfo.shapes[idx];
        uint8_t slot = mutable_action->params.lfo.slot;
        
        if (slot == 1 || slot == 3) lfo_set_waveform(0, (lfo_waveform_t)shape);
        if (slot == 2 || slot == 3) lfo_set_waveform(1, (lfo_waveform_t)shape);
        
        ESP_LOGI(TAG, "LFO Shape: slot %d, shape %d", slot, shape);
        
        // Advance to next shape
        mutable_action->params.lfo.current_index = (idx + 1) % num_shapes;
      }
      break;

    case ACTION_CLOCK_TOGGLE:
      if (is_press) {
        scene_t* scene = scene_get_current();
        if (scene) {
          // Toggle the current scene's send_clock state
          scene->send_clock = !scene->send_clock;
          ESP_LOGI(TAG, "Clock Toggle: send_clock now %s",
            scene->send_clock ? "enabled" : "disabled");
        }
      }
      break;

    case ACTION_CLOCK_HOLD:
      {
        scene_t* scene = scene_get_current();
        if (scene) {
          // start_enabled determines what happens on press
          // If start_enabled=true: press enables clock, release disables
          // If start_enabled=false: press disables clock, release enables
          bool press_state = action->params.clock.start_enabled;
          scene->send_clock = is_press ? press_state : !press_state;
          ESP_LOGI(TAG, "Clock Hold: send_clock now %s",
            scene->send_clock ? "enabled" : "disabled");
        }
      }
      break;

    case ACTION_CLOCK_BURST:
      if (is_press) {
        // Start sending extra clock pulses at the configured speed
        clock_burst_start(action->params.clock_burst.speed_percent);
      } else {
        // Stop the burst
        clock_burst_stop();
      }
      break;

    case ACTION_CUT_TOGGLE:
      if (is_press) {
        uint8_t cut_mode = action->params.cut.cut_mode;
        // Toggle cut state based on mode
        if (cut_mode == 0 || cut_mode == 2) {
          // Toggle local cut
          bool current = midi_out_get_cut_local();
          midi_out_set_cut_local(!current);
        }
        if (cut_mode == 1 || cut_mode == 2) {
          // Toggle passthrough cut
          bool current = midi_out_get_cut_passthrough();
          midi_out_set_cut_passthrough(!current);
        }
        ESP_LOGI(TAG, "Cut Toggle: mode %d", cut_mode);
      }
      break;

    case ACTION_CUT_HOLD:
      {
        uint8_t cut_mode = action->params.cut.cut_mode;
        bool cut_active = is_press;  // Cut when pressed, restore on release
        if (cut_mode == 0 || cut_mode == 2) {
          midi_out_set_cut_local(cut_active);
        }
        if (cut_mode == 1 || cut_mode == 2) {
          midi_out_set_cut_passthrough(cut_active);
        }
        ESP_LOGI(TAG, "Cut Hold: mode %d, cut %s",
          cut_mode, cut_active ? "active" : "released");
      }
      break;

    case ACTION_SET_UI:
      if (is_press) {
        uint8_t idx = action->params.ui.module;
        if (idx < ui_scene_selectable_module_count) {
          ui_draw_module_t* mod = ui_get_module_by_name(
            ui_scene_selectable_modules[idx]);
          if (mod) {
            ui_set_draw_module(mod);
            ESP_LOGI(TAG, "Set UI: %s",
              ui_scene_selectable_modules[idx]);
          }
        }
      }
      break;

    case ACTION_UI_HOLD:
      {
        uint8_t idx = is_press
          ? action->params.ui.module
          : action->params.ui.module2;
        if (idx < ui_scene_selectable_module_count) {
          ui_draw_module_t* mod = ui_get_module_by_name(
            ui_scene_selectable_modules[idx]);
          if (mod) {
            ui_set_draw_module(mod);
            ESP_LOGI(TAG, "UI Hold: %s (%s)",
              ui_scene_selectable_modules[idx],
              is_press ? "press" : "release");
          }
        }
      }
      break;

    case ACTION_UI_CYCLE:
      if (is_press) {
        action_t* mutable = (action_t*)action;
        uint8_t num = mutable->params.ui.num_modules;
        if (num < 2) num = 2;  // Guard against div-by-zero
        uint8_t idx = mutable->params.ui.modules[
          mutable->params.ui.current_index % num];
        if (idx < ui_scene_selectable_module_count) {
          ui_draw_module_t* mod = ui_get_module_by_name(
            ui_scene_selectable_modules[idx]);
          if (mod) {
            ui_set_draw_module(mod);
            ESP_LOGI(TAG, "UI Cycle: %s (step %d/%d)",
              ui_scene_selectable_modules[idx],
              mutable->params.ui.current_index + 1, num);
          }
        }
        mutable->params.ui.current_index =
          (mutable->params.ui.current_index + 1) % num;
      }
      break;

    case ACTION_PARAM_HOLD:
      {
        scene_t* scene = scene_get_current();
        if (scene) {
          uint8_t cc = is_press
            ? action->params.tw_param.param
            : action->params.tw_param.param2;
          scene->touchwheel.cc_numbers[0] = cc;
          // Restore touchwheel value to this CC's cached value
          uint8_t cached_value = action_get_cc_value(cc);
          scene_set_touchwheel_value(cached_value);
          ESP_LOGI(TAG, "Param Hold: CC %u = %u (%s)",
            (unsigned)cc, (unsigned)cached_value,
            is_press ? "press" : "release");
        }
      }
      break;

    case ACTION_PARAM_CYCLE:
      if (is_press) {
        scene_t* scene = scene_get_current();
        if (scene) {
          action_t* mutable = (action_t*)action;
          uint8_t num = mutable->params.tw_param.num_params;
          if (num < 2) num = 2;  // Guard against div-by-zero
          uint8_t cc = mutable->params.tw_param.params[
            mutable->params.tw_param.current_index % num];
          scene->touchwheel.cc_numbers[0] = cc;
          // Restore touchwheel value to this CC's cached value
          uint8_t cached_value = action_get_cc_value(cc);
          scene_set_touchwheel_value(cached_value);
          ESP_LOGI(TAG, "Param Cycle: CC %u = %u (step %d/%d)",
            (unsigned)cc, (unsigned)cached_value,
            mutable->params.tw_param.current_index + 1, num);
          mutable->params.tw_param.current_index =
            (mutable->params.tw_param.current_index + 1) % num;
        }
      }
      break;

    default:
      ESP_LOGW(TAG, "Unhandled action type: %d", action->type);
      return ESP_ERR_NOT_SUPPORTED;
  }
  
  return ESP_OK;
}

// Helper functions to create common actions
action_t action_create_control(uint8_t cc_number, uint8_t value) {
  action_t action = {0};
  action.type = ACTION_CONTROL_CHANGE;
  action.params.control.num_ccs = 1;
  action.params.control.cc_numbers[0] = cc_number;
  action.params.control.values[0] = value;
  return action;
}

action_t action_create_control_hold(uint8_t cc_number, uint8_t press_value, uint8_t release_value) {
  action_t action = {0};
  action.type = ACTION_CONTROL_HOLD;
  action.params.control.num_ccs = 1;
  action.params.control.cc_numbers[0] = cc_number;
  action.params.control.values[0] = press_value;
  action.params.control.values2[0] = release_value;
  return action;
}

action_t action_create_preset_inc(void) {
  action_t action = {0};
  action.type = ACTION_PRESET_INC;
  return action;
}

action_t action_create_preset_dec(void) {
  action_t action = {0};
  action.type = ACTION_PRESET_DEC;
  return action;
}

action_t action_create_scene_inc(void) {
  action_t action = {0};
  action.type = ACTION_SCENE_INC;
  return action;
}

action_t action_create_scene_dec(void) {
  action_t action = {0};
  action.type = ACTION_SCENE_DEC;
  return action;
}

action_t action_create_tap(void) {
  action_t action = {0};
  action.type = ACTION_TAP;
  return action;
}

action_t action_create_tap_tempo(void) {
  action_t action = {0};
  action.type = ACTION_TAP_TEMPO;
  return action;
}

action_t action_create_set_tempo(uint16_t bpm) {
  action_t action = {0};
  action.type = ACTION_SET_TEMPO;
  action.params.tempo.bpm = bpm;
  return action;
}

action_t action_create_transport(action_type_t transport_type) {
  action_t action = {0};
  action.type = transport_type;  // ACTION_PLAY, STOP, etc.
  return action;
}

action_t action_create_reset(void) {
  action_t action = {0};
  action.type = ACTION_RESET;
  return action;
}

action_t action_create_sustain(void) {
  action_t action = {0};
  action.type = ACTION_SUSTAIN;
  return action;
}

action_t action_create_sostenuto(void) {
  action_t action = {0};
  action.type = ACTION_SOSTENUTO;
  return action;
}

action_t action_create_touchwheel_hold(uint8_t press_mode, uint8_t release_mode) {
  action_t action = {0};
  action.type = ACTION_TOUCHWHEEL_HOLD;
  action.params.tw_mode.mode = press_mode;
  action.params.tw_mode.mode2 = release_mode;
  return action;
}

action_t action_create_lfo_start(uint8_t slot) {
  action_t action = {0};
  action.type = ACTION_LFO_START;
  action.params.lfo.slot = slot;
  return action;
}

action_t action_create_lfo_stop(uint8_t slot) {
  action_t action = {0};
  action.type = ACTION_LFO_STOP;
  action.params.lfo.slot = slot;
  return action;
}

action_t action_create_lfo_toggle(uint8_t slot) {
  action_t action = {0};
  action.type = ACTION_LFO_TOGGLE;
  action.params.lfo.slot = slot;
  return action;
}

action_t action_create_clock_toggle(bool start_enabled) {
  action_t action = {0};
  action.type = ACTION_CLOCK_TOGGLE;
  action.params.clock.start_enabled = start_enabled;
  return action;
}

action_t action_create_clock_hold(bool press_enables) {
  action_t action = {0};
  action.type = ACTION_CLOCK_HOLD;
  action.params.clock.start_enabled = press_enables;
  return action;
}

action_t action_create_clock_burst(uint8_t speed_percent) {
  action_t action = {0};
  action.type = ACTION_CLOCK_BURST;
  action.params.clock_burst.speed_percent = speed_percent;
  return action;
}

action_t action_create_cut_toggle(uint8_t cut_mode) {
  action_t action = {0};
  action.type = ACTION_CUT_TOGGLE;
  action.params.cut.cut_mode = cut_mode;
  return action;
}

action_t action_create_cut_hold(uint8_t cut_mode) {
  action_t action = {0};
  action.type = ACTION_CUT_HOLD;
  action.params.cut.cut_mode = cut_mode;
  return action;
}

action_t action_create_set_ui(uint8_t module_index) {
  action_t action = {0};
  action.type = ACTION_SET_UI;
  action.params.ui.module = module_index;
  return action;
}

action_t action_create_ui_hold(uint8_t press_module, uint8_t release_module) {
  action_t action = {0};
  action.type = ACTION_UI_HOLD;
  action.params.ui.module = press_module;
  action.params.ui.module2 = release_module;
  return action;
}

// Actions that require press/release (hold) behavior
// These should NOT be assigned to bump or on_load
static const action_type_t hold_actions[] = {
  ACTION_CONTROL_HOLD,
  ACTION_PRESET_HOLD,
  ACTION_TEMPO_HOLD,
  ACTION_NOTE,
  ACTION_TOUCHWHEEL_HOLD,
  ACTION_SUSTAIN,
  ACTION_SOSTENUTO,
  ACTION_LFO_TOGGLE,  // Toggle needs discrete press
  ACTION_LFO_SHAPE,   // Shape cycle needs discrete press
  ACTION_CLOCK_HOLD,
  ACTION_CLOCK_BURST,
  ACTION_CUT_HOLD,
  ACTION_UI_HOLD,
  ACTION_PARAM_HOLD,
};

bool action_requires_hold(action_type_t type) {
  for (size_t i = 0; i < sizeof(hold_actions) / sizeof(hold_actions[0]); i++) {
    if (hold_actions[i] == type) return true;
  }
  return false;
}

// Check if an action type is valid for a specific trigger
// Enforces restrictions for touchwheel mode actions and hold actions
bool action_is_valid_for_trigger(action_type_t type, action_trigger_type_t trigger) {
  // ACTION_NONE is always valid (clear assignment)
  if (type == ACTION_NONE) return true;
  
  // Hold actions are invalid for bump and on_load (no release event)
  if (action_requires_hold(type)) {
    if (trigger == ACTION_TRIGGER_BUMP || trigger == ACTION_TRIGGER_ON_LOAD) {
      return false;
    }
  }
  
  // Touchwheel Hold restrictions:
  // - Valid: Pads 8-11, Buttons, Expression switch
  // - Invalid: Pads 0-7, Bump, on_load
  if (type == ACTION_TOUCHWHEEL_HOLD) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }
  
  // Touchwheel Cycle restrictions:
  // - Valid: Pads 8-11, Buttons, Bump, Expression switch
  // - Invalid: Pads 0-7, on_load
  if (type == ACTION_TOUCHWHEEL_CYCLE) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_BUMP:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }
  
  // UI actions cannot be assigned to on_load
  if (type == ACTION_SET_UI || type == ACTION_UI_CYCLE) {
    if (trigger == ACTION_TRIGGER_ON_LOAD) return false;
  }
  
  // Param Hold restrictions:
  // - Valid: Pads 8-11, Buttons, Expression switch
  // - Invalid: Pads 0-7, Bump, on_load
  if (type == ACTION_PARAM_HOLD) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }
  
  // Param Cycle restrictions:
  // - Valid: Pads 8-11, Buttons, Bump, Expression switch
  // - Invalid: Pads 0-7, on_load
  if (type == ACTION_PARAM_CYCLE) {
    switch (trigger) {
      case ACTION_TRIGGER_TOUCHPAD_8_11:
      case ACTION_TRIGGER_BUTTON:
      case ACTION_TRIGGER_BUMP:
      case ACTION_TRIGGER_EXPR_SWITCH:
        return true;
      default:
        return false;
    }
  }
  
  return true;
}

// Returns true for actions that support timing options (non-HOLD actions)
// HOLD actions must execute immediately to preserve press/release pairing
// TAP_TEMPO is always immediate (toggles tap mode instantly)
bool action_supports_timing(action_type_t type) {
  if (type == ACTION_NONE || type == ACTION_TAP_TEMPO) return false;
  return !action_requires_hold(type);
}

// Returns true for actions that support repeat options
// Preset/scene actions support timing but NOT repeat
// TAP_TEMPO never repeats (it's a mode toggle)
bool action_supports_repeat(action_type_t type) {
  if (type == ACTION_NONE || action_requires_hold(type)) return false;
  switch (type) {
    case ACTION_PRESET:
    case ACTION_PRESET_INC:
    case ACTION_PRESET_DEC:
    case ACTION_PRESET_CYCLE:
    case ACTION_SCENE:
    case ACTION_SCENE_INC:
    case ACTION_SCENE_DEC:
    case ACTION_TAP_TEMPO:
    case ACTION_SET_UI:
      return false;
    default:
      return true;
  }
}

// Static buffer for timing string
static char s_timing_str[16];

const char* action_timing_to_string(action_timing_t timing, uint8_t beat) {
  switch (timing) {
    case ACTION_TIMING_IMMEDIATE:
      return "immediate";
    case ACTION_TIMING_NEXT_BEAT:
      return "beat";
    case ACTION_TIMING_SPECIFIC_BEAT:
      snprintf(s_timing_str, sizeof(s_timing_str), "beat_%d", beat);
      return s_timing_str;
    default:
      return "immediate";
  }
}

void action_timing_from_string(const char* str, action_timing_t* timing, uint8_t* beat) {
  // Default to immediate
  *timing = ACTION_TIMING_IMMEDIATE;
  *beat = 1;
  
  if (!str || str[0] == '\0') return;
  
  if (strcmp(str, "immediate") == 0) {
    *timing = ACTION_TIMING_IMMEDIATE;
  } else if (strcmp(str, "beat") == 0) {
    *timing = ACTION_TIMING_NEXT_BEAT;
  } else if (strncmp(str, "beat_", 5) == 0) {
    // Parse beat number from "beat_N"
    int beat_num = atoi(str + 5);
    if (beat_num >= 1 && beat_num <= 16) {
      *timing = ACTION_TIMING_SPECIFIC_BEAT;
      *beat = (uint8_t)beat_num;
    } else {
      // Invalid beat number, default to beat 1
      *timing = ACTION_TIMING_SPECIFIC_BEAT;
      *beat = 1;
    }
  }
}

bool action_validate_timing(action_t* action, uint8_t beats_per_bar) {
  if (!action) return false;
  if (action->timing != ACTION_TIMING_SPECIFIC_BEAT) return false;
  if (action->timing_beat <= beats_per_bar) return false;
  
  ESP_LOGW(TAG, "Beat %d invalid for %d-beat bar, remapping to beat 1",
    action->timing_beat, beats_per_bar);
  action->timing_beat = 1;
  return true;  // Remapping occurred
}

// Validate all action timings in a scene against its time signature
void action_validate_scene_timings(scene_t* scene) {
  if (!scene) return;
  
  uint8_t beats = scene->time_signature.numerator;
  if (beats == 0) beats = 4;  // Default to 4/4
  
  int remapped = 0;
  
  // Validate touchpad actions
  for (int i = 0; i < NUM_TOUCHPADS; i++) {
    if (action_validate_timing(&scene->touchpads[i].action, beats)) remapped++;
  }
  
  // Validate button actions
  if (action_validate_timing(&scene->button_left, beats)) remapped++;
  if (action_validate_timing(&scene->button_right, beats)) remapped++;
  if (action_validate_timing(&scene->button_both, beats)) remapped++;
  
  // Validate bump action
  if (action_validate_timing(&scene->bump, beats)) remapped++;
  
  // Validate on_load actions
  for (int i = 0; i < scene->num_on_load_actions && i < MAX_ON_LOAD_ACTIONS; i++) {
    if (action_validate_timing(&scene->on_load[i], beats)) remapped++;
  }
  
  // Validate expression mode actions
  if (action_validate_timing(&scene->sustain, beats)) remapped++;
  if (action_validate_timing(&scene->sostenuto, beats)) remapped++;
  if (action_validate_timing(&scene->expr_switch, beats)) remapped++;
  
  if (remapped > 0) {
    ESP_LOGW(TAG, "Remapped %d action(s) with invalid beat timings to beat 1", remapped);
  }
}

// ============================================================================
// Repeat Division Helpers
// ============================================================================

const char* action_repeat_division_to_string(action_repeat_division_t div) {
  switch (div) {
    case ACTION_REPEAT_16_BARS:   return "16_bars";
    case ACTION_REPEAT_12_BARS:   return "12_bars";
    case ACTION_REPEAT_8_BARS:    return "8_bars";
    case ACTION_REPEAT_4_BARS:    return "4_bars";
    case ACTION_REPEAT_2_BARS:    return "2_bars";
    case ACTION_REPEAT_1_BAR:     return "1_bar";
    case ACTION_REPEAT_HALF:      return "half";
    case ACTION_REPEAT_QUARTER:   return "quarter";
    case ACTION_REPEAT_EIGHTH:    return "eighth";
    case ACTION_REPEAT_SIXTEENTH: return "sixteenth";
    case ACTION_REPEAT_32ND:      return "32nd";
    default: return "quarter";
  }
}

action_repeat_division_t action_repeat_division_from_string(const char* str) {
  if (!str) return ACTION_REPEAT_QUARTER;
  if (strcmp(str, "16_bars") == 0) return ACTION_REPEAT_16_BARS;
  if (strcmp(str, "12_bars") == 0) return ACTION_REPEAT_12_BARS;
  if (strcmp(str, "8_bars") == 0) return ACTION_REPEAT_8_BARS;
  if (strcmp(str, "4_bars") == 0) return ACTION_REPEAT_4_BARS;
  if (strcmp(str, "2_bars") == 0) return ACTION_REPEAT_2_BARS;
  if (strcmp(str, "1_bar") == 0) return ACTION_REPEAT_1_BAR;
  if (strcmp(str, "half") == 0) return ACTION_REPEAT_HALF;
  if (strcmp(str, "quarter") == 0) return ACTION_REPEAT_QUARTER;
  if (strcmp(str, "eighth") == 0) return ACTION_REPEAT_EIGHTH;
  if (strcmp(str, "sixteenth") == 0) return ACTION_REPEAT_SIXTEENTH;
  if (strcmp(str, "32nd") == 0) return ACTION_REPEAT_32ND;
  return ACTION_REPEAT_QUARTER;
}

// Get repeat interval in beats
// For divisions >= 1 bar, returns beats_per_bar * bars
// For divisions < 1 bar, returns 0 (will use sub-beat timing)
uint8_t action_repeat_division_to_beats(action_repeat_division_t div, uint8_t beats_per_bar) {
  if (beats_per_bar == 0) beats_per_bar = 4;
  
  switch (div) {
    case ACTION_REPEAT_16_BARS:   return beats_per_bar * 16;
    case ACTION_REPEAT_12_BARS:   return beats_per_bar * 12;
    case ACTION_REPEAT_8_BARS:    return beats_per_bar * 8;
    case ACTION_REPEAT_4_BARS:    return beats_per_bar * 4;
    case ACTION_REPEAT_2_BARS:    return beats_per_bar * 2;
    case ACTION_REPEAT_1_BAR:     return beats_per_bar;
    case ACTION_REPEAT_HALF:      return 2;  // 2 quarter notes
    case ACTION_REPEAT_QUARTER:   return 1;  // 1 quarter note
    case ACTION_REPEAT_EIGHTH:    return 0;  // Sub-beat (handled separately)
    case ACTION_REPEAT_SIXTEENTH: return 0;
    case ACTION_REPEAT_32ND:      return 0;
    default: return 1;
  }
}

// ============================================================================
// Morph System Implementation
// ============================================================================

// Calculate optimal step count based on value delta and duration
static uint8_t calculate_auto_steps(uint8_t value_delta, uint32_t duration_ms) {
  // Base: 1 step per 2 values of delta for smoothness
  uint8_t delta_based = (value_delta + 1) / 2;
  
  // Duration modifier: ~20-40ms per step is musically smooth
  uint8_t duration_based = (uint8_t)(duration_ms / 25);
  
  // Use the smaller of the two to avoid excessive MIDI traffic
  // but ensure enough steps for smooth transitions
  uint8_t steps = (delta_based < duration_based) ? delta_based : duration_based;
  
  // Clamp to reasonable range
  if (steps < 4) steps = 4;
  if (steps > 64) steps = 64;
  
  return steps;
}

// Find the index of a value in a discrete values array
// Returns the index of the closest matching value, or 0 if not found
static int find_discrete_index(const uint8_t* values, uint8_t count, uint8_t target) {
  if (!values || count == 0) return 0;
  
  int best_idx = 0;
  int best_diff = 255;
  
  for (int i = 0; i < count; i++) {
    int diff = (target > values[i]) ? (target - values[i]) : (values[i] - target);
    if (diff < best_diff) {
      best_diff = diff;
      best_idx = i;
    }
  }
  
  return best_idx;
}

// Get morph duration for FEEL mode (curated, tempo-scaled)
static uint32_t get_feel_duration_ms(morph_feel_t feel, uint16_t bpm) {
  if (bpm == 0) bpm = 120;
  uint32_t beat_ms = 60000 / bpm;
  
  switch (feel) {
    case MORPH_FEEL_FAST:   return beat_ms / 4;  // 16th note
    case MORPH_FEEL_MEDIUM: return beat_ms;       // Quarter note
    case MORPH_FEEL_SLOW:   return beat_ms * 2;   // Half note
    default: return beat_ms;
  }
}

// Get morph duration for DURATION mode (fixed musical duration)
static uint32_t get_duration_ms(morph_division_t div, uint16_t bpm) {
  if (bpm == 0) bpm = 120;
  uint32_t beat_ms = 60000 / bpm;
  uint8_t felt_beats = tempo_get_felt_beats_per_bar();
  if (felt_beats == 0) felt_beats = 4;
  
  switch (div) {
    // Beat-based durations
    case MORPH_DIV_1_BEAT:  return beat_ms;
    case MORPH_DIV_2_BEATS: return beat_ms * 2;
    case MORPH_DIV_3_BEATS: return beat_ms * 3;
    
    // Bar-based durations
    case MORPH_DIV_1_BAR:   return beat_ms * felt_beats;
    case MORPH_DIV_2_BARS:  return beat_ms * felt_beats * 2;
    case MORPH_DIV_3_BARS:  return beat_ms * felt_beats * 3;
    case MORPH_DIV_4_BARS:  return beat_ms * felt_beats * 4;
    
    // Beat targets not applicable for duration mode, default to 1 beat
    default: return beat_ms;
  }
}

// Get morph duration for SYNC mode (time until target moment)
static uint32_t get_sync_duration_ms(morph_division_t div, uint16_t bpm,
    uint8_t current_beat, uint8_t beats_per_bar) {
  if (bpm == 0) bpm = 120;
  if (beats_per_bar == 0) beats_per_bar = 4;
  if (current_beat == 0) current_beat = 1;
  
  uint32_t beat_ms = 60000 / bpm;
  uint8_t target_beat;
  uint8_t beats_remaining;
  
  switch (div) {
    case MORPH_DIV_BEAT:
      // Next beat (any) - minimum 1 beat
      return beat_ms;
      
    case MORPH_DIV_BAR:
      // Next bar (beat 1)
      target_beat = 1;
      if (current_beat >= target_beat) {
        beats_remaining = beats_per_bar - current_beat + target_beat;
      } else {
        beats_remaining = target_beat - current_beat;
      }
      if (beats_remaining == 0) beats_remaining = beats_per_bar;
      return beat_ms * beats_remaining;
      
    case MORPH_DIV_2_BARS:
      // Next bar start + 1 more bar
      beats_remaining = beats_per_bar - current_beat + 1;
      beats_remaining += beats_per_bar;
      return beat_ms * beats_remaining;
      
    case MORPH_DIV_4_BARS:
      // Next bar start + 3 more bars
      beats_remaining = beats_per_bar - current_beat + 1;
      beats_remaining += beats_per_bar * 3;
      return beat_ms * beats_remaining;
      
    case MORPH_DIV_BEAT_2:
    case MORPH_DIV_BEAT_3:
    case MORPH_DIV_BEAT_4:
      // Specific beat target
      target_beat = (div - MORPH_DIV_BEAT_2) + 2;
      if (target_beat > beats_per_bar) target_beat = beats_per_bar;
      if (current_beat >= target_beat) {
        // Target is in next bar
        beats_remaining = beats_per_bar - current_beat + target_beat;
      } else {
        beats_remaining = target_beat - current_beat;
      }
      if (beats_remaining == 0) beats_remaining = beats_per_bar;
      return beat_ms * beats_remaining;
      
    default:
      return beat_ms;
  }
}

// Get the target beat for SYNC mode
static uint8_t get_sync_target_beat(morph_division_t div) {
  switch (div) {
    case MORPH_DIV_BEAT:   return 0;  // Any beat
    case MORPH_DIV_BAR:    return 1;  // Beat 1 (bar start)
    case MORPH_DIV_2_BARS: return 1;  // Beat 1
    case MORPH_DIV_4_BARS: return 1;  // Beat 1
    case MORPH_DIV_BEAT_2: return 2;
    case MORPH_DIV_BEAT_3: return 3;
    case MORPH_DIV_BEAT_4: return 4;
    default: return 0;
  }
}

// Get step count based on mode
static uint8_t get_morph_steps(const action_t* action, uint8_t value_delta, 
    uint32_t duration_ms) {
  switch (action->morph_steps_mode) {
    case MORPH_STEPS_AUTO:
      return calculate_auto_steps(value_delta, duration_ms);
    case MORPH_STEPS_COARSE:
      return 8;
    case MORPH_STEPS_MEDIUM:
      return 16;
    case MORPH_STEPS_FINE:
      return 32;
    case MORPH_STEPS_MANUAL:
      return (action->morph_manual_steps >= 8 && action->morph_manual_steps <= 128) ?
        action->morph_manual_steps : 32;
    default:
      return 16;
  }
}

// Start or restart the morph timer if any morphs are active
static void morph_update_timer(void) {
  // Timer must exist to update
  if (!s_morph_timer) {
    ESP_LOGW(TAG, "Morph timer not initialized");
    return;
  }
  
  bool any_active = false;
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (s_active_morphs[i].active && !s_active_morphs[i].sync_waiting_final) {
      any_active = true;
      break;
    }
  }
  
  if (any_active) {
    if (!esp_timer_is_active(s_morph_timer)) {
      // Start timer at 10ms interval for responsive updates
      esp_timer_start_periodic(s_morph_timer, 10000);  // 10ms in microseconds
    }
  } else {
    if (esp_timer_is_active(s_morph_timer)) {
      esp_timer_stop(s_morph_timer);
    }
  }
}

// Send the final target values for a morph
static void morph_send_final_values(active_morph_t* m) {
  uint8_t channel = device_config_get_channel() - 1;
  
  for (int i = 0; i < m->num_ccs && i < 4; i++) {
    uint8_t target = m->target_values[i];
    
    // Only send if value differs from last sent (deduplication)
    if (target != m->last_sent_values[i]) {
      send_control_change(channel, m->cc_numbers[i], target);
      s_last_cc_values[m->cc_numbers[i]] = target;
      m->last_sent_values[i] = target;
    }
  }
  
  ESP_LOGD(TAG, "Morph completed: CC%d -> %d", 
    m->cc_numbers[0], m->target_values[0]);
}

// Advance a morph by one step
static void morph_advance_step(active_morph_t* m) {
  if (!m->active) return;
  
  m->current_step++;
  
  // Check if this is the final step
  if (m->current_step >= m->total_steps) {
    if (m->timing_mode == MORPH_TIMING_SYNC && m->sync_target_beat != 0) {
      // SYNC mode: wait for beat event to send final value
      m->sync_waiting_final = true;
      ESP_LOGD(TAG, "Morph waiting for beat %d to complete", m->sync_target_beat);
    } else {
      // Send final values immediately
      morph_send_final_values(m);
      m->active = false;
    }
    return;
  }
  
  // Calculate intermediate values with discrete-aware interpolation
  uint8_t channel = device_config_get_channel() - 1;
  
  for (int i = 0; i < m->num_ccs && i < 4; i++) {
    uint8_t new_value;
    
    if (m->discrete_counts[i] > 0) {
      // Discrete parameter: interpolate between discrete indices
      
      // For delay_final params (≤4 discrete values), stay on start until last step
      if (m->delay_final[i]) {
        // Stay on start value until the very last step (handled by final values)
        new_value = m->start_values[i];
      } else {
        // Multiple discrete values: interpolate between indices
        float progress = (float)m->current_step / m->total_steps;
        
        int start_idx = find_discrete_index(m->discrete_values[i],
          m->discrete_counts[i], m->start_values[i]);
        int target_idx = find_discrete_index(m->discrete_values[i],
          m->discrete_counts[i], m->target_values[i]);
        
        // Interpolate between discrete indices
        int idx_range = target_idx - start_idx;
        int current_idx = start_idx + (int)(idx_range * progress);
        
        // Clamp to valid range
        if (current_idx < 0) current_idx = 0;
        if (current_idx >= m->discrete_counts[i]) current_idx = m->discrete_counts[i] - 1;
        
        new_value = m->discrete_values[i][current_idx];
      }
    } else {
      // Continuous parameter: linear interpolation
      int16_t start = m->start_values[i];
      int16_t target = m->target_values[i];
      int16_t range = target - start;
      
      int16_t value = start + (range * m->current_step) / m->total_steps;
      
      // Clamp to 0-127
      if (value < 0) value = 0;
      if (value > 127) value = 127;
      
      new_value = (uint8_t)value;
    }
    
    // Only send if value changed (deduplication)
    if (new_value != m->last_sent_values[i]) {
      send_control_change(channel, m->cc_numbers[i], new_value);
      s_last_cc_values[m->cc_numbers[i]] = new_value;
      m->last_sent_values[i] = new_value;
    }
  }
}

// Timer callback - check and advance all active morphs
static void morph_timer_callback(void* arg) {
  (void)arg;
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);  // Convert to ms
  
  bool any_active = false;
  
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    active_morph_t* m = &s_active_morphs[i];
    if (!m->active) continue;
    if (m->sync_waiting_final) continue;  // Waiting for beat event
    
    any_active = true;
    
    // Check if it's time for next step
    if (now >= m->next_step_time) {
      morph_advance_step(m);
      m->next_step_time = now + m->step_interval_ms;
    }
  }
  
  // Stop timer if no more active morphs
  if (!any_active) {
    morph_update_timer();
  }
}

// Beat event handler for SYNC mode completion
static void morph_beat_event_handler(const event_t* event, void* context) {
  (void)context;
  if (event->type != EVENT_BEAT) return;
  
  uint8_t current_beat = event->data.beat.beat_in_bar;
  
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    active_morph_t* m = &s_active_morphs[i];
    if (!m->active) continue;
    if (!m->sync_waiting_final) continue;
    
    bool hit_target = false;
    
    if (m->sync_target_beat == 0) {
      // Any beat triggers completion
      hit_target = true;
    } else if (m->sync_target_beat == current_beat) {
      // Specific beat matches
      hit_target = true;
    }
    
    if (hit_target) {
      morph_send_final_values(m);
      m->active = false;
      ESP_LOGD(TAG, "Morph SYNC completed on beat %d", current_beat);
    }
  }
}

// Find an existing morph for the same CC(s) or an empty slot
static active_morph_t* find_or_create_morph_slot(uint8_t num_ccs,
    const uint8_t* cc_numbers) {
  // First, look for existing morph on same CC(s) to cancel it
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (s_active_morphs[i].active) {
      // Check if any CC overlaps
      for (int j = 0; j < s_active_morphs[i].num_ccs && j < 4; j++) {
        for (int k = 0; k < num_ccs && k < 4; k++) {
          if (s_active_morphs[i].cc_numbers[j] == cc_numbers[k]) {
            // Found overlap - reuse this slot
            ESP_LOGD(TAG, "Canceling existing morph for CC%d", cc_numbers[k]);
            return &s_active_morphs[i];
          }
        }
      }
    }
  }
  
  // Find empty slot
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    if (!s_active_morphs[i].active) {
      return &s_active_morphs[i];
    }
  }
  
  ESP_LOGW(TAG, "No morph slot available");
  return NULL;
}

// Start a morph transition
static bool morph_start(const action_t* action, uint8_t num_ccs,
    const uint8_t* cc_numbers, const uint8_t* target_values) {
  if (!action->morph_enabled) return false;
  if (num_ccs == 0 || num_ccs > 4) return false;
  
  active_morph_t* m = find_or_create_morph_slot(num_ccs, cc_numbers);
  if (!m) return false;
  
  // Get current tempo and beat info
  uint16_t bpm = tempo_get_bpm();
  if (bpm == 0) bpm = 120;
  
  time_signature_t sig = tempo_get_time_signature();
  uint8_t beats_per_bar = sig.numerator;
  if (beats_per_bar == 0) beats_per_bar = 4;
  
  uint8_t current_beat = transport_get_current_beat();
  if (current_beat == 0) current_beat = 1;
  
  // Calculate duration based on timing mode
  uint32_t duration_ms;
  switch (action->morph_timing_mode) {
    case MORPH_TIMING_FEEL:
      duration_ms = get_feel_duration_ms(action->morph_feel, bpm);
      break;
    case MORPH_TIMING_DURATION:
      duration_ms = get_duration_ms(action->morph_division, bpm);
      break;
    case MORPH_TIMING_SYNC:
      duration_ms = get_sync_duration_ms(action->morph_division, bpm,
        current_beat, beats_per_bar);
      break;
    default:
      duration_ms = 500;
      break;
  }
  
  // Calculate max value delta across all CCs
  uint8_t max_delta = 0;
  for (int i = 0; i < num_ccs && i < 4; i++) {
    uint8_t start = s_last_cc_values[cc_numbers[i]];
    uint8_t target = target_values[i];
    uint8_t delta = (start > target) ? (start - target) : (target - start);
    if (delta > max_delta) max_delta = delta;
  }
  
  // Get step count
  uint8_t steps = get_morph_steps(action, max_delta, duration_ms);
  
  // Avoid division by zero
  if (steps == 0) steps = 1;
  
  // Calculate step interval
  uint32_t step_interval = duration_ms / steps;
  if (step_interval < 10) step_interval = 10;  // Minimum 10ms between steps
  
  // Initialize morph state
  m->active = true;
  m->action = (action_t*)action;
  m->num_ccs = num_ccs;
  m->current_step = 0;
  m->total_steps = steps;
  m->step_interval_ms = step_interval;
  m->timing_mode = action->morph_timing_mode;
  m->sync_target_beat = (action->morph_timing_mode == MORPH_TIMING_SYNC) ?
    get_sync_target_beat(action->morph_division) : 0;
  m->sync_waiting_final = false;
  
  // Get device definition for discrete value lookup
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  for (int i = 0; i < num_ccs && i < 4; i++) {
    m->cc_numbers[i] = cc_numbers[i];
    m->start_values[i] = s_last_cc_values[cc_numbers[i]];
    m->target_values[i] = target_values[i];
    m->last_sent_values[i] = m->start_values[i];  // Track for deduplication
    
    // Look up control definition for discrete values
    const midi_control_t* ctrl = device ?
      assets_get_control_by_cc(device, cc_numbers[i]) : NULL;
    
    if (ctrl && ctrl->discrete_count > 0 && ctrl->discrete_values) {
      // Copy discrete values (up to MORPH_MAX_DISCRETE)
      uint8_t dcount = ctrl->discrete_count;
      if (dcount > MORPH_MAX_DISCRETE) dcount = MORPH_MAX_DISCRETE;
      m->discrete_counts[i] = dcount;
      for (int j = 0; j < dcount; j++) {
        m->discrete_values[i][j] = (uint8_t)ctrl->discrete_values[j].value;
      }
      // Delay final value for params with ≤4 discrete values
      m->delay_final[i] = (ctrl->discrete_count <= 4);
    } else {
      // Continuous parameter
      m->discrete_counts[i] = 0;
      m->delay_final[i] = false;
    }
  }
  
  // Set first step time
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
  m->next_step_time = now + step_interval;
  
  // Send first value immediately (start value) - track for deduplication
  uint8_t channel = device_config_get_channel() - 1;
  for (int i = 0; i < num_ccs && i < 4; i++) {
    send_control_change(channel, cc_numbers[i], m->start_values[i]);
    m->last_sent_values[i] = m->start_values[i];
  }
  
  // Start/update timer
  morph_update_timer();
  
  ESP_LOGD(TAG, "Morph started: CC%d %d->%d, %d steps, %lu ms interval",
    cc_numbers[0], m->start_values[0], m->target_values[0],
    (int)steps, (unsigned long)step_interval);
  
  return true;
}

// Clear all active morphs
void action_clear_morphs(void) {
  for (int i = 0; i < MAX_ACTIVE_MORPHS; i++) {
    s_active_morphs[i].active = false;
  }
  morph_update_timer();
  ESP_LOGD(TAG, "Cleared all active morphs");
}

// Check if action type supports morphing
bool action_supports_morph(action_type_t type) {
  switch (type) {
    case ACTION_CONTROL_HOLD:
    case ACTION_CONTROL_CYCLE:
    case ACTION_RANDOMIZE:
      return true;
    default:
      return false;
  }
}

// ============================================================================
// Morph String Conversion Functions
// ============================================================================

const char* morph_steps_mode_to_string(morph_steps_mode_t mode) {
  switch (mode) {
    case MORPH_STEPS_AUTO:   return "auto";
    case MORPH_STEPS_COARSE: return "coarse";
    case MORPH_STEPS_MEDIUM: return "medium";
    case MORPH_STEPS_FINE:   return "fine";
    case MORPH_STEPS_MANUAL: return "manual";
    default: return "auto";
  }
}

morph_steps_mode_t morph_steps_mode_from_string(const char* str) {
  if (!str) return MORPH_STEPS_AUTO;
  if (strcmp(str, "auto") == 0) return MORPH_STEPS_AUTO;
  if (strcmp(str, "coarse") == 0) return MORPH_STEPS_COARSE;
  if (strcmp(str, "medium") == 0) return MORPH_STEPS_MEDIUM;
  if (strcmp(str, "fine") == 0) return MORPH_STEPS_FINE;
  if (strcmp(str, "manual") == 0) return MORPH_STEPS_MANUAL;
  return MORPH_STEPS_AUTO;
}

const char* morph_timing_mode_to_string(morph_timing_mode_t mode) {
  switch (mode) {
    case MORPH_TIMING_FEEL:     return "feel";
    case MORPH_TIMING_DURATION: return "duration";
    case MORPH_TIMING_SYNC:     return "sync";
    default: return "feel";
  }
}

morph_timing_mode_t morph_timing_mode_from_string(const char* str) {
  if (!str) return MORPH_TIMING_FEEL;
  if (strcmp(str, "feel") == 0) return MORPH_TIMING_FEEL;
  if (strcmp(str, "duration") == 0) return MORPH_TIMING_DURATION;
  if (strcmp(str, "sync") == 0) return MORPH_TIMING_SYNC;
  return MORPH_TIMING_FEEL;
}

const char* morph_feel_to_string(morph_feel_t feel) {
  switch (feel) {
    case MORPH_FEEL_FAST:   return "fast";
    case MORPH_FEEL_MEDIUM: return "medium";
    case MORPH_FEEL_SLOW:   return "slow";
    default: return "medium";
  }
}

morph_feel_t morph_feel_from_string(const char* str) {
  if (!str) return MORPH_FEEL_MEDIUM;
  if (strcmp(str, "fast") == 0) return MORPH_FEEL_FAST;
  if (strcmp(str, "medium") == 0) return MORPH_FEEL_MEDIUM;
  if (strcmp(str, "slow") == 0) return MORPH_FEEL_SLOW;
  return MORPH_FEEL_MEDIUM;
}

const char* morph_division_to_string(morph_division_t div) {
  switch (div) {
    case MORPH_DIV_1_BEAT:  return "1_beat";
    case MORPH_DIV_1_BAR:   return "1_bar";
    case MORPH_DIV_2_BARS:  return "2_bars";
    case MORPH_DIV_4_BARS:  return "4_bars";
    case MORPH_DIV_BEAT_2:  return "beat_2";
    case MORPH_DIV_BEAT_3:  return "beat_3";
    case MORPH_DIV_BEAT_4:  return "beat_4";
    case MORPH_DIV_2_BEATS: return "2_beats";
    case MORPH_DIV_3_BEATS: return "3_beats";
    case MORPH_DIV_3_BARS:  return "3_bars";
    default: return "1_bar";
  }
}

morph_division_t morph_division_from_string(const char* str) {
  if (!str) return MORPH_DIV_1_BAR;
  // New format
  if (strcmp(str, "1_beat") == 0) return MORPH_DIV_1_BEAT;
  if (strcmp(str, "1_bar") == 0) return MORPH_DIV_1_BAR;
  if (strcmp(str, "2_beats") == 0) return MORPH_DIV_2_BEATS;
  if (strcmp(str, "3_beats") == 0) return MORPH_DIV_3_BEATS;
  if (strcmp(str, "3_bars") == 0) return MORPH_DIV_3_BARS;
  // Legacy format (backward compatibility)
  if (strcmp(str, "beat") == 0) return MORPH_DIV_1_BEAT;
  if (strcmp(str, "bar") == 0) return MORPH_DIV_1_BAR;
  if (strcmp(str, "2_bars") == 0) return MORPH_DIV_2_BARS;
  if (strcmp(str, "4_bars") == 0) return MORPH_DIV_4_BARS;
  if (strcmp(str, "beat_2") == 0) return MORPH_DIV_BEAT_2;
  if (strcmp(str, "beat_3") == 0) return MORPH_DIV_BEAT_3;
  if (strcmp(str, "beat_4") == 0) return MORPH_DIV_BEAT_4;
  return MORPH_DIV_1_BAR;
}

// ============================================================================
// CC Value Cache API
// ============================================================================

uint8_t action_get_cc_value(uint8_t cc_num) {
  if (cc_num >= 128) return 0;
  return s_last_cc_values[cc_num];
}

void action_set_cc_value(uint8_t cc_num, uint8_t value) {
  if (cc_num >= 128) return;
  s_last_cc_values[cc_num] = value;
}

void action_reset_cc_values(const void* device) {
  // Cast to device_def_t - passed as void* to avoid header dependency
  const device_def_t* dev = (const device_def_t*)device;
  
  if (!dev || dev->control_count == 0) {
    // No device: reset all to 0
    memset(s_last_cc_values, 0, sizeof(s_last_cc_values));
    return;
  }
  
  // Reset all to 0, then set device-specific min values
  memset(s_last_cc_values, 0, sizeof(s_last_cc_values));
  
  for (uint16_t i = 0; i < dev->control_count; i++) {
    const midi_control_t* ctrl = &dev->controls[i];
    if (ctrl->type == MIDI_CONTROL_TYPE_CC && ctrl->id < 128) {
      // Use the device's min value for this CC
      s_last_cc_values[ctrl->id] = (uint8_t)ctrl->min;
    }
  }
}
