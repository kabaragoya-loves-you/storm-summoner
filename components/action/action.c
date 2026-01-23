#include "action.h"
#include "midi_messages.h"
#include "midi_lfo_scene_handler.h"
#include "device_config.h"
#include "scene.h"
#include "touchwheel_mode_mapping.h"
#include "transport.h"
#include "tempo.h"
#include "assets_manager.h"
#include "lfo.h"
#include "event_bus.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "action";

static bool s_initialized = false;

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
  
  // Phase 2: Repeating (placeholder for future)
  // bool repeating;          // True if this action repeats after firing
  // uint8_t repeat_division; // Subdivision for repeat interval
  // uint32_t next_fire_tick; // Tick count for next fire (multi-bar tracking)
  
  // Phase 3: Probability (placeholder for future)
  // uint8_t probability;     // 0-100 chance of firing
  
  // Phase 4: Step patterns (placeholder for future)
  // uint8_t pattern_length;  // 1-8 steps in pattern
  // uint8_t pattern_mask;    // Bitmask of active steps
  // uint8_t pattern_step;    // Current position in pattern
} pending_action_t;

static pending_action_t s_pending_actions[MAX_PENDING_ACTIONS];

// Forward declaration for internal execute (bypasses timing check)
static esp_err_t action_execute_immediate(const action_t* action, uint8_t trigger_value, bool is_press);

// Beat event handler - fires pending actions when beat matches
static void handle_beat_event(const event_t* event, void* context) {
  if (event->type != EVENT_BEAT) return;
  
  uint8_t current_beat = event->data.beat.beat_in_bar;
  
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) continue;
    
    pending_action_t* pending = &s_pending_actions[i];
    bool should_fire = false;
    
    if (pending->target_beat == 0) {
      // Any beat triggers
      should_fire = true;
    } else if (pending->target_beat == current_beat) {
      // Specific beat matches
      should_fire = true;
    }
    
    if (should_fire) {
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
      
      // Clear this slot (Phase 2 would re-queue here for repeating actions)
      pending->valid = false;
    }
  }
}

// Enqueue an action for delayed execution
static bool action_enqueue_pending(action_t* action, uint8_t trigger_value, uint8_t target_beat) {
  // Find empty slot
  for (int i = 0; i < MAX_PENDING_ACTIONS; i++) {
    if (!s_pending_actions[i].valid) {
      s_pending_actions[i].action = *action;
      s_pending_actions[i].original = action;  // Store pointer to original for state sync
      s_pending_actions[i].trigger_value = trigger_value;
      s_pending_actions[i].target_beat = target_beat;
      s_pending_actions[i].valid = true;
      
      ESP_LOGD(TAG, "Queued action %s for beat %d (slot %d)",
        action_type_to_string(action->type),
        target_beat == 0 ? -1 : target_beat, i);
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
  ESP_LOGD(TAG, "Cleared pending action queue");
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
  [ACTION_TOUCHWHEEL_MODE] = "Touchwheel",
  [ACTION_TOUCHWHEEL_HOLD] = "Touchwheel Hold",
  [ACTION_TOUCHWHEEL_CYCLE] = "Touchwheel Cycle",
  [ACTION_LFO_START] = "LFO Start",
  [ACTION_LFO_STOP] = "LFO Stop",
  [ACTION_LFO_TOGGLE] = "LFO Toggle",
  [ACTION_LFO_SHAPE] = "LFO Shape"
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
    
    // Cast away const - cycle actions need state sync back to original
    if (action_enqueue_pending((action_t*)action, trigger_value, target_beat)) {
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
  
  switch (action->type) {
    // Preset control
    case ACTION_PRESET_INC:
      if (is_press) device_config_program_next();
      break;
      
    case ACTION_PRESET_DEC:
      if (is_press) device_config_program_prev();
      break;
      
    case ACTION_PRESET:
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
      
    // Scene control
    case ACTION_SCENE_INC:
      if (is_press) scene_next();
      break;
      
    case ACTION_SCENE_DEC:
      if (is_press) scene_previous();
      break;
      
    case ACTION_SCENE:
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
          send_control_change(channel, action->params.control.cc_numbers[i],
            action->params.control.values[i]);
          ESP_LOGD(TAG, "Sent CC%d=%d", action->params.control.cc_numbers[i],
            action->params.control.values[i]);
        }
      }
      break;
      
    case ACTION_CONTROL_HOLD:
      // Send value on press, value2 on release (momentary hold behavior)
      {
        uint8_t num_ccs = action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;  // Backward compat
        for (int i = 0; i < num_ccs && i < 4; i++) {
          uint8_t value = is_press ?
            action->params.control.values[i] : action->params.control.values2[i];
          send_control_change(channel, action->params.control.cc_numbers[i], value);
          ESP_LOGD(TAG, "CC%d hold: %d", action->params.control.cc_numbers[i], value);
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
        
        // Send current cycle value for each CC
        for (int i = 0; i < num_ccs && i < 4; i++) {
          uint8_t value = mutable_action->params.control.cycle_values[i][idx];
          send_control_change(channel, mutable_action->params.control.cc_numbers[i], value);
          ESP_LOGD(TAG, "Cycled CC%d to %d", mutable_action->params.control.cc_numbers[i], value);
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
        
        for (int i = 0; i < action->params.randomize.num_ccs; i++) {
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
          
          send_control_change(channel, cc, random_val);
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, random_val);
        }
        ESP_LOGD(TAG, "Randomized %d CCs", action->params.randomize.num_ccs);
      }
      break;
      
    // System
    case ACTION_CONFIRM_PENDING:
      if (is_press) {
        if (scene_get_mode() == SCENE_MODE_SINGLE) {
          if (device_config_has_pending_program()) device_config_confirm_program();
        } else {
          if (scene_has_pending_change()) scene_confirm_change();
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
    case ACTION_TOUCHWHEEL_MODE:
      // Set touchwheel mode on press only (using user-facing mode index)
      // Uses runtime function - no persistence (changes are temporary)
      if (is_press) {
        uint8_t user_mode_idx = action->params.tw_mode.mode;
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
          ESP_LOGD(TAG, "Set touchwheel mode to %s", mapping->display_name);
        }
      }
      break;
      
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

action_t action_create_touchwheel_mode(uint8_t mode) {
  action_t action = {0};
  action.type = ACTION_TOUCHWHEEL_MODE;
  action.params.tw_mode.mode = mode;
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
};

bool action_requires_hold(action_type_t type) {
  for (size_t i = 0; i < sizeof(hold_actions) / sizeof(hold_actions[0]); i++) {
    if (hold_actions[i] == type) return true;
  }
  return false;
}

// Returns true for actions that support timing options (non-HOLD actions)
// HOLD actions must execute immediately to preserve press/release pairing
bool action_supports_timing(action_type_t type) {
  return !action_requires_hold(type) && type != ACTION_NONE;
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
