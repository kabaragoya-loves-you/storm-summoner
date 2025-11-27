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
  [ACTION_PROGRAM_SET] = "Program Set",
  [ACTION_PROGRAM_BANK_SET] = "Program Bank Set",
  [ACTION_SCENE_NEXT] = "Scene Next",
  [ACTION_SCENE_PREV] = "Scene Prev",
  [ACTION_SCENE_SET] = "Scene Set",
  [ACTION_TRANSPORT_PLAY] = "Transport Play",
  [ACTION_TRANSPORT_STOP] = "Transport Stop",
  [ACTION_TRANSPORT_PAUSE] = "Transport Pause",
  [ACTION_TRANSPORT_RECORD] = "Transport Record",
  [ACTION_TRANSPORT_TOGGLE] = "Transport Toggle",
  [ACTION_TAP_TEMPO] = "Tap Tempo",
  [ACTION_TEMPO_NUDGE_UP] = "Tempo Nudge Up",
  [ACTION_TEMPO_NUDGE_DOWN] = "Tempo Nudge Down",
  [ACTION_SEND_CC] = "Send CC",
  [ACTION_SEND_CC_HOLD] = "CC Hold",
  [ACTION_SEND_CC_CYCLE] = "CC Cycle",
  [ACTION_SEND_NOTE_ON] = "Note On",
  [ACTION_SEND_NOTE_OFF] = "Note Off",
  [ACTION_SEND_PC] = "Send PC",
  [ACTION_RANDOMIZE_CC] = "Randomize CC",
  [ACTION_RANDOMIZE_MULTI] = "Randomize Multi",
  [ACTION_SEND_DOUBLE_CC] = "Send 14-bit CC",
  [ACTION_SEND_NRPN] = "Send NRPN",
  [ACTION_SEND_RPN] = "Send RPN",
  [ACTION_SEND_PITCH_BEND] = "Pitch Bend",
  [ACTION_SEND_AFTERTOUCH] = "Aftertouch",
  [ACTION_SEND_POLY_AFTERTOUCH] = "Poly Aftertouch",
  [ACTION_SEND_SONG_SELECT] = "Song Select",
  [ACTION_SEND_SONG_POSITION] = "Song Position",
  [ACTION_SEND_MMC] = "MMC",
  [ACTION_SEND_CLOCK_START] = "MIDI Start",
  [ACTION_SEND_CLOCK_STOP] = "MIDI Stop",
  [ACTION_SEND_CLOCK_CONTINUE] = "MIDI Continue",
  [ACTION_SEND_RESET] = "System Reset",
  [ACTION_SEND_TUNE_REQUEST] = "Tune Request",
  [ACTION_SCREENSAVER_TOGGLE] = "Screensaver Toggle",
  [ACTION_CONFIRM_PENDING] = "Confirm Pending",
  [ACTION_CANCEL_PENDING] = "Cancel Pending",
  [ACTION_ALL_NOTES_OFF] = "All Notes Off",
  [ACTION_ALL_SOUND_OFF] = "All Sound Off",
  [ACTION_SUSTAIN] = "Sustain",
  [ACTION_SOSTENUTO] = "Sostenuto"
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
  return (type < ACTION_MAX) ? action_type_names[type] : "Unknown";
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
      if (is_press) device_config_set_program(action->params.target.number);
      break;
      
    case ACTION_PROGRAM_BANK_SET:
      if (is_press) {
        // Send bank select + program change for presets > 127
        // Uses the current bank_select_mode setting
        uint16_t preset = action->params.preset.preset_number;
        device_config_set_preset(preset);
        ESP_LOGI(TAG, "Set preset %u (bank %d, program %d)", 
                 (unsigned)preset, preset / 128, preset % 128);
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
      if (is_press) scene_set_current(action->params.target.number);
      break;
      
    // Transport
    case ACTION_TRANSPORT_PLAY:
      if (is_press) transport_play();
      break;
      
    case ACTION_TRANSPORT_STOP:
      if (is_press) transport_stop();
      break;
      
    case ACTION_TRANSPORT_PAUSE:
      if (is_press) transport_pause();
      break;
      
    case ACTION_TRANSPORT_RECORD:
      if (is_press) transport_record();
      break;
      
    case ACTION_TRANSPORT_TOGGLE:
      if (is_press) transport_toggle();
      break;
      
    // Tempo
    case ACTION_TAP_TEMPO:
      if (is_press) tempo_tap_event();
      break;
      
    case ACTION_TEMPO_NUDGE_UP:
      if (is_press) {
        uint8_t delta = action->params.tempo.bpm_delta ? action->params.tempo.bpm_delta : 1;
        uint16_t current_bpm = tempo_get_bpm();
        tempo_set_bpm(current_bpm + delta);
      }
      break;
      
    case ACTION_TEMPO_NUDGE_DOWN:
      if (is_press) {
        uint8_t delta = action->params.tempo.bpm_delta ? action->params.tempo.bpm_delta : 1;
        uint16_t current_bpm = tempo_get_bpm();
        if (current_bpm > delta) tempo_set_bpm(current_bpm - delta);
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
      
    case ACTION_SEND_PC:
      if (is_press) {
        send_program_change(channel, action->params.target.number);
        ESP_LOGD(TAG, "Sent PC %d", action->params.target.number);
      }
      break;
      
    // Extended MIDI messages
    case ACTION_SEND_PITCH_BEND:
      if (is_press) {
        send_pitch_bend(channel, action->params.pitch_bend.value);
        ESP_LOGD(TAG, "Sent pitch bend: %d", action->params.pitch_bend.value);
      }
      break;
      
    case ACTION_SEND_AFTERTOUCH:
      if (is_press) {
        send_channel_aftertouch(channel, action->params.aftertouch.pressure);
        ESP_LOGD(TAG, "Sent aftertouch: %d", action->params.aftertouch.pressure);
      }
      break;
      
    case ACTION_SEND_POLY_AFTERTOUCH:
      if (is_press) {
        send_poly_aftertouch(channel, action->params.aftertouch.note, action->params.aftertouch.pressure);
        ESP_LOGD(TAG, "Sent poly aftertouch: note=%d pressure=%d", 
                 action->params.aftertouch.note, action->params.aftertouch.pressure);
      }
      break;
      
    case ACTION_SEND_DOUBLE_CC:
      if (is_press) {
        send_double_control_change(channel, action->params.double_cc.msb_cc, 
                                   action->params.double_cc.lsb_cc, action->params.double_cc.value);
        ESP_LOGD(TAG, "Sent 14-bit CC%d/%d: %d", action->params.double_cc.msb_cc, 
                 action->params.double_cc.lsb_cc, action->params.double_cc.value);
      }
      break;
      
    case ACTION_SEND_NRPN:
      if (is_press) {
        send_nrpn(channel, action->params.nrpn.parameter, action->params.nrpn.value);
        ESP_LOGD(TAG, "Sent NRPN %d: %d", action->params.nrpn.parameter, action->params.nrpn.value);
      }
      break;
      
    case ACTION_SEND_RPN:
      if (is_press) {
        send_rpn(channel, action->params.nrpn.parameter, action->params.nrpn.value);
        ESP_LOGD(TAG, "Sent RPN %d: %d", action->params.nrpn.parameter, action->params.nrpn.value);
      }
      break;
      
    case ACTION_SEND_SONG_SELECT:
      if (is_press) {
        send_song_select(action->params.target.number);
        ESP_LOGD(TAG, "Sent song select: %d", action->params.target.number);
      }
      break;
      
    case ACTION_SEND_SONG_POSITION:
      if (is_press) {
        send_song_position(action->params.song_pos.position);
        ESP_LOGD(TAG, "Sent song position: %d", action->params.song_pos.position);
      }
      break;
      
    case ACTION_SEND_MMC:
      if (is_press) {
        send_mmc(action->params.mmc.command);
        ESP_LOGD(TAG, "Sent MMC command: 0x%02X", action->params.mmc.command);
      }
      break;
      
    // MIDI System messages
    case ACTION_SEND_CLOCK_START:
      if (is_press) {
        send_start();
        ESP_LOGD(TAG, "Sent MIDI Clock Start");
      }
      break;
      
    case ACTION_SEND_CLOCK_STOP:
      if (is_press) {
        send_stop();
        ESP_LOGD(TAG, "Sent MIDI Clock Stop");
      }
      break;
      
    case ACTION_SEND_CLOCK_CONTINUE:
      if (is_press) {
        send_continue();
        ESP_LOGD(TAG, "Sent MIDI Clock Continue");
      }
      break;
      
    case ACTION_SEND_RESET:
      if (is_press) {
        send_reset();
        ESP_LOGD(TAG, "Sent System Reset");
      }
      break;
      
    case ACTION_SEND_TUNE_REQUEST:
      if (is_press) {
        send_tune_request();
        ESP_LOGD(TAG, "Sent Tune Request");
      }
      break;
      
    // Randomization
    case ACTION_RANDOMIZE_CC:
      if (is_press) {
        uint8_t random_val = esp_random() % 128;
        send_control_change(channel, action->params.cc.cc_number, random_val);
        ESP_LOGI(TAG, "Randomized CC%d to %d", action->params.cc.cc_number, random_val);
      }
      break;
      
    case ACTION_RANDOMIZE_MULTI:
      if (is_press) {
        for (int i = 0; i < action->params.multi_random.num_ccs; i++) {
          uint8_t cc = action->params.multi_random.cc_numbers[i];
          uint8_t min = action->params.multi_random.min_values[i];
          uint8_t max = action->params.multi_random.max_values[i];
          uint8_t range = max - min + 1;
          uint8_t random_val = min + (esp_random() % range);
          send_control_change(channel, cc, random_val);
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, random_val);
        }
        ESP_LOGI(TAG, "Randomized %d CCs", action->params.multi_random.num_ccs);
      }
      break;
      
    // System
    case ACTION_SCREENSAVER_TOGGLE:
      if (is_press) {
        // Toggle screensaver - check current state and switch
        // For now just disable/enable (no toggle function exists)
        ESP_LOGW(TAG, "Screensaver toggle not fully implemented");
        // TODO: Track screensaver state and toggle
      }
      break;
      
    case ACTION_CONFIRM_PENDING:
      if (is_press) {
        if (scene_get_mode() == SCENE_MODE_SINGLE) {
          if (device_config_has_pending_program()) device_config_confirm_program();
        } else {
          if (scene_has_pending_change()) scene_confirm_change();
        }
      }
      break;
      
    case ACTION_CANCEL_PENDING:
      if (is_press) {
        if (scene_get_mode() == SCENE_MODE_SINGLE) {
          if (device_config_has_pending_program()) device_config_cancel_pending_program();
        } else {
          if (scene_has_pending_change()) scene_cancel_pending();
        }
      }
      break;
      
    case ACTION_ALL_NOTES_OFF:
      if (is_press) {
        send_control_change(channel, 123, 0);  // CC123 = All Notes Off
        ESP_LOGD(TAG, "Sent All Notes Off");
      }
      break;
      
    case ACTION_ALL_SOUND_OFF:
      if (is_press) {
        send_control_change(channel, 120, 0);  // CC120 = All Sound Off
        ESP_LOGD(TAG, "Sent All Sound Off");
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

action_t action_create_tap_tempo(void) {
  action_t action = {0};
  action.type = ACTION_TAP_TEMPO;
  return action;
}

action_t action_create_transport(action_type_t transport_type) {
  action_t action = {0};
  action.type = transport_type;  // ACTION_TRANSPORT_PLAY, STOP, etc.
  return action;
}

action_t action_create_all_notes_off(void) {
  action_t action = {0};
  action.type = ACTION_ALL_NOTES_OFF;
  return action;
}

action_t action_create_all_sound_off(void) {
  action_t action = {0};
  action.type = ACTION_ALL_SOUND_OFF;
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

