#include "action.h"
#include "midi_messages.h"
#include "device_config.h"
#include "scene.h"
#include "transport.h"
#include "tempo.h"
#include "screensaver.h"
#include "esp_log.h"
#include "esp_random.h"

static const char* TAG = "action";

static bool s_initialized = false;

// Action type names for debugging
static const char* action_type_names[] = {
  [ACTION_NONE] = "None",
  [ACTION_PROGRAM_NEXT] = "Program Next",
  [ACTION_PROGRAM_PREV] = "Program Prev",
  [ACTION_PROGRAM_SET] = "PC",
  [ACTION_SCENE_NEXT] = "Scene Next",
  [ACTION_SCENE_PREV] = "Scene Prev",
  [ACTION_SCENE_SET] = "Scene Set",
  [ACTION_PLAY] = "Play",
  [ACTION_STOP] = "Stop",
  [ACTION_PAUSE] = "Pause",
  [ACTION_RECORD] = "Record",
  [ACTION_TAP] = "Tap",
  [ACTION_TAP_TEMPO] = "Tap Tempo",
  [ACTION_SET_TEMPO] = "Set Tempo",
  [ACTION_TEMPO_INC] = "Tempo +1",
  [ACTION_TEMPO_DEC] = "Tempo -1",
  [ACTION_SEND_CC] = "Send CC",
  [ACTION_SEND_CC_HOLD] = "CC Hold",
  [ACTION_SEND_CC_CYCLE] = "CC Cycle",
  [ACTION_SEND_NOTE_ON] = "Note On",
  [ACTION_SEND_NOTE_OFF] = "Note Off",
  [ACTION_RANDOMIZE_CC] = "Randomize CC",
  [ACTION_CONFIRM_PENDING] = "Confirm Pending",
  [ACTION_RESET] = "Reset",
  [ACTION_SUSTAIN] = "Sustain",
  [ACTION_SOSTENUTO] = "Sostenuto",
  [ACTION_TOUCHWHEEL_MODE] = "TW Mode",
  [ACTION_TOUCHWHEEL_MODE_HOLD] = "TW Mode Hold",
  [ACTION_TOUCHWHEEL_MODE_CYCLE] = "TW Mode Cycle"
};

esp_err_t action_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "Action system already initialized");
    return ESP_OK;
  }
  
  ESP_LOGI(TAG, "Initializing action system");
  s_initialized = true;
  
  return ESP_OK;
}

const char* action_type_to_string(action_type_t type) {
  if (type >= ACTION_MAX) return "Unknown";
  const char* name = action_type_names[type];
  return name ? name : "Unknown";
}

esp_err_t action_execute(const action_t* action, uint8_t trigger_value, bool is_press) {
  if (!action || action->type == ACTION_NONE) {
    return ESP_OK;
  }
  
  ESP_LOGD(TAG, "Executing action: %s (trigger=%d, press=%d)", 
           action_type_to_string(action->type), trigger_value, is_press);
  
  uint8_t channel = device_config_get_channel() - 1;  // MIDI uses 0-based channels
  
  switch (action->type) {
    // Program control
    case ACTION_PROGRAM_NEXT:
      if (is_press) device_config_program_next();
      break;
      
    case ACTION_PROGRAM_PREV:
      if (is_press) device_config_program_prev();
      break;
      
    case ACTION_PROGRAM_SET:
      if (is_press) {
        // Smart PC: uses bank mode setting to decide behavior
        uint16_t program = action->params.pc.program;
        if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
          // Bank mode: treat as preset 0-16383
          device_config_set_preset(program);
        } else {
          // No bank: treat as program 0-127
          device_config_set_program(program & 0x7F);
        }
      }
      break;
      
    // Scene control
    case ACTION_SCENE_NEXT:
      if (is_press) scene_next();
      break;
      
    case ACTION_SCENE_PREV:
      if (is_press) scene_previous();
      break;
      
    case ACTION_SCENE_SET:
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
      
    // MIDI CC actions
    case ACTION_SEND_CC:
      // Send CC value on press only (one-shot)
      if (is_press) {
        send_control_change(channel, action->params.cc.cc_number, action->params.cc.value);
        ESP_LOGD(TAG, "Sent CC%d=%d", action->params.cc.cc_number, action->params.cc.value);
      }
      break;
      
    case ACTION_SEND_CC_HOLD:
      // Send value1 on press, value2 on release (momentary hold behavior)
      {
        uint8_t value = is_press ? action->params.cc.value : action->params.cc.value2;
        send_control_change(channel, action->params.cc.cc_number, value);
        ESP_LOGD(TAG, "CC%d hold: %d", action->params.cc.cc_number, value);
      }
      break;
      
    case ACTION_SEND_CC_CYCLE:
      if (is_press) {
        // Note: This modifies the action state - works because action is passed by pointer through the chain
        action_t* mutable_action = (action_t*)action;  // Cast away const for state tracking
        uint8_t idx = mutable_action->params.cc.current_index;
        uint8_t value = mutable_action->params.cc.values[idx];
        send_control_change(channel, mutable_action->params.cc.cc_number, value);
        
        // Advance to next value
        mutable_action->params.cc.current_index = (idx + 1) % mutable_action->params.cc.num_values;
        
        ESP_LOGD(TAG, "Cycled CC%d to %d (next: %d)", mutable_action->params.cc.cc_number, value, 
                 mutable_action->params.cc.values[mutable_action->params.cc.current_index]);
      }
      break;
      
    // Note actions
    case ACTION_SEND_NOTE_ON:
      if (is_press) {
        send_note_on(channel, action->params.note.note, action->params.note.velocity);
        ESP_LOGD(TAG, "Note On: %d vel=%d", action->params.note.note, action->params.note.velocity);
      }
      break;
      
    case ACTION_SEND_NOTE_OFF:
      if (!is_press) {
        send_note_off(channel, action->params.note.note, 0);
        ESP_LOGD(TAG, "Note Off: %d", action->params.note.note);
      }
      break;
      
    // Randomization
    case ACTION_RANDOMIZE_CC:
      if (is_press) {
        for (int i = 0; i < action->params.randomize.num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          uint8_t random_val = esp_random() % 128;
          send_control_change(channel, cc, random_val);
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, random_val);
        }
        ESP_LOGI(TAG, "Randomized %d CCs", action->params.randomize.num_ccs);
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
      // Set touchwheel mode on press only
      if (is_press) {
        uint8_t scene_index = scene_get_current_index();
        scene_set_touchwheel_mode(scene_index, (touchwheel_mode_t)action->params.tw_mode.mode);
        ESP_LOGD(TAG, "Set touchwheel mode to %d", action->params.tw_mode.mode);
      }
      break;
      
    case ACTION_TOUCHWHEEL_MODE_HOLD:
      // Set mode on press, restore mode2 on release
      {
        uint8_t scene_index = scene_get_current_index();
        uint8_t mode = is_press ? action->params.tw_mode.mode : action->params.tw_mode.mode2;
        scene_set_touchwheel_mode(scene_index, (touchwheel_mode_t)mode);
        ESP_LOGD(TAG, "Touchwheel mode hold: %d", mode);
      }
      break;
      
    case ACTION_TOUCHWHEEL_MODE_CYCLE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t idx = mutable_action->params.tw_mode.current_index;
        uint8_t mode = mutable_action->params.tw_mode.modes[idx];
        
        uint8_t scene_index = scene_get_current_index();
        scene_set_touchwheel_mode(scene_index, (touchwheel_mode_t)mode);
        
        // Advance to next mode
        mutable_action->params.tw_mode.current_index =
          (idx + 1) % mutable_action->params.tw_mode.num_modes;
        
        ESP_LOGD(TAG, "Cycled touchwheel mode to %d (next idx: %d)", mode,
          mutable_action->params.tw_mode.current_index);
      }
      break;
      
    default:
      ESP_LOGW(TAG, "Unhandled action type: %d", action->type);
      return ESP_ERR_NOT_SUPPORTED;
  }
  
  return ESP_OK;
}

esp_err_t action_execute_chain(const action_chain_t* chain, uint8_t trigger_value, bool is_press) {
  if (!chain || chain->num_actions == 0) {
    return ESP_OK;
  }
  
  ESP_LOGD(TAG, "Executing action chain with %d actions", chain->num_actions);
  
  for (int i = 0; i < chain->num_actions; i++) {
    esp_err_t ret = action_execute(&chain->actions[i], trigger_value, is_press);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_SUPPORTED) {
      ESP_LOGE(TAG, "Action %d in chain failed: %s", i, esp_err_to_name(ret));
      // Continue executing remaining actions even if one fails
    }
  }
  
  return ESP_OK;
}

// Helper functions to create common actions
action_t action_create_send_cc(uint8_t cc_number, uint8_t value) {
  action_t action = {0};
  action.type = ACTION_SEND_CC;
  action.params.cc.cc_number = cc_number;
  action.params.cc.value = value;
  return action;
}

action_t action_create_cc_hold(uint8_t cc_number, uint8_t press_value, uint8_t release_value) {
  action_t action = {0};
  action.type = ACTION_SEND_CC_HOLD;
  action.params.cc.cc_number = cc_number;
  action.params.cc.value = press_value;
  action.params.cc.value2 = release_value;
  return action;
}

action_t action_create_program_next(void) {
  action_t action = {0};
  action.type = ACTION_PROGRAM_NEXT;
  return action;
}

action_t action_create_program_prev(void) {
  action_t action = {0};
  action.type = ACTION_PROGRAM_PREV;
  return action;
}

action_t action_create_scene_next(void) {
  action_t action = {0};
  action.type = ACTION_SCENE_NEXT;
  return action;
}

action_t action_create_scene_prev(void) {
  action_t action = {0};
  action.type = ACTION_SCENE_PREV;
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

action_t action_create_touchwheel_mode_hold(uint8_t press_mode, uint8_t release_mode) {
  action_t action = {0};
  action.type = ACTION_TOUCHWHEEL_MODE_HOLD;
  action.params.tw_mode.mode = press_mode;
  action.params.tw_mode.mode2 = release_mode;
  return action;
}
