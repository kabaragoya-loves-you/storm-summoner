#include "action_internal.h"
#include "midi_messages.h"
#include "midi_lfo_scene_handler.h"
#include "midi_out.h"
#include "scene.h"
#include "config.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "esp_log.h"

static const char* TAG = "action_handlers_modulation";

action_handle_result_t action_handlers_modulation_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;

  switch (action->type) {
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
      return ACTION_HANDLED;

    case ACTION_LFO_STOP:
      if (is_press) {
        uint8_t slot = action->params.lfo.slot;
        scene_t* scene = scene_get_current();
        if (slot == 1 || slot == 3) {
          if (lfo_is_enabled(0)) {
            if (lfo_get_restore_on_stop(0)) {
              midi_lfo_scene_handler_restore_value(0);
            }
            lfo_enable(0, false);
            if (scene) scene->lfo1.enabled = false;
          } else if (lfo_is_pending_start(0)) {
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
      return ACTION_HANDLED;

    case ACTION_LFO_TOGGLE:
      if (is_press) {
        uint8_t slot = action->params.lfo.slot;
        scene_t* scene = scene_get_current();
        if (slot == 1 || slot == 3) {
          bool new_state = !lfo_is_enabled(0);
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
      return ACTION_HANDLED;

    case ACTION_LFO_SHAPE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_shapes = mutable_action->params.lfo.num_shapes;

        if (num_shapes < 2 || num_shapes > 8) {
          ESP_LOGW(TAG, "LFO Shape: num_shapes=%d invalid, skipping", num_shapes);
          return ACTION_HANDLED;
        }

        uint8_t idx = mutable_action->params.lfo.current_index;
        if (idx >= num_shapes || idx >= 8) idx = 0;

        uint8_t shape = mutable_action->params.lfo.shapes[idx];
        uint8_t slot = mutable_action->params.lfo.slot;

        if (slot == 1 || slot == 3) lfo_set_waveform(0, (lfo_waveform_t)shape);
        if (slot == 2 || slot == 3) lfo_set_waveform(1, (lfo_waveform_t)shape);

        ESP_LOGI(TAG, "LFO Shape: slot %d, shape %d", slot, shape);

        mutable_action->params.lfo.current_index = (idx + 1) % num_shapes;
      }
      return ACTION_HANDLED;

    case ACTION_CLOCK_TOGGLE:
      if (is_press) {
        scene_t* scene = scene_get_current();
        if (scene) {
          scene->send_clock = !scene->send_clock;
          ESP_LOGI(TAG, "Clock Toggle: send_clock now %s",
            scene->send_clock ? "enabled" : "disabled");
        }
      }
      return ACTION_HANDLED;

    case ACTION_CLOCK_HOLD: {
      scene_t* scene = scene_get_current();
      if (scene) {
        bool press_state = action->params.clock.start_enabled;
        scene->send_clock = is_press ? press_state : !press_state;
        ESP_LOGI(TAG, "Clock Hold: send_clock now %s",
          scene->send_clock ? "enabled" : "disabled");
      }
      return ACTION_HANDLED;
    }

    case ACTION_CLOCK_BURST:
      if (is_press) {
        action_clock_burst_start(action->params.clock_burst.speed_percent);
      } else {
        action_clock_burst_stop();
      }
      return ACTION_HANDLED;

    case ACTION_CUT_TOGGLE:
      if (is_press) {
        uint8_t cut_mode = action->params.cut.cut_mode;
        if (cut_mode == 0 || cut_mode == 2) {
          bool current = midi_out_get_cut_local();
          midi_out_set_cut_local(!current);
        }
        if (cut_mode == 1 || cut_mode == 2) {
          bool current = midi_out_get_cut_passthrough();
          midi_out_set_cut_passthrough(!current);
        }
        ESP_LOGI(TAG, "Cut Toggle: mode %d", cut_mode);
      }
      return ACTION_HANDLED;

    case ACTION_CUT_HOLD: {
      uint8_t cut_mode = action->params.cut.cut_mode;
      bool cut_active = is_press;
      if (cut_mode == 0 || cut_mode == 2) {
        midi_out_set_cut_local(cut_active);
      }
      if (cut_mode == 1 || cut_mode == 2) {
        midi_out_set_cut_passthrough(cut_active);
      }
      ESP_LOGI(TAG, "Cut Hold: mode %d, cut %s",
        cut_mode, cut_active ? "active" : "released");
      return ACTION_HANDLED;
    }

    case ACTION_RTG_TOGGLE:
      if (is_press) {
        rtg_toggle();
        ESP_LOGI(TAG, "RTG Toggle: now %s", rtg_is_running() ? "running" : "stopped");
      }
      return ACTION_HANDLED;

    case ACTION_RTG_HOLD:
      if (is_press) {
        rtg_start();
        ESP_LOGD(TAG, "RTG Hold: press -> running");
      } else {
        rtg_stop();
        ESP_LOGD(TAG, "RTG Hold: release -> stopped");
      }
      return ACTION_HANDLED;

    case ACTION_SAMPLE_HOLD_TOGGLE:
      if (is_press) {
        sample_hold_toggle();
        ESP_LOGI(TAG, "S+H Toggle: now %s", sample_hold_is_running() ? "running" : "stopped");
      }
      return ACTION_HANDLED;

    case ACTION_SAMPLE_HOLD_HOLD:
      if (is_press) {
        sample_hold_start();
        ESP_LOGD(TAG, "S+H Hold: press -> running");
      } else {
        sample_hold_stop();
        ESP_LOGD(TAG, "S+H Hold: release -> stopped");
      }
      return ACTION_HANDLED;

    case ACTION_STEP:
      if (is_press) {
        if (action->params.step.target == STEP_TARGET_RTG) {
          rtg_step();
          ESP_LOGD(TAG, "Step action: RTG");
        } else if (action->params.step.target == STEP_TARGET_SH) {
          sample_hold_step();
          ESP_LOGD(TAG, "Step action: S+H");
        }
      }
      return ACTION_HANDLED;

    case ACTION_PUNCH_IN:
      if (is_press) {
        action_punch_in_start(action);
      }
      return ACTION_HANDLED;

    case ACTION_BOOMERANG:
      if (is_press) {
        action_boomerang_start_internal(action);
      }
      return ACTION_HANDLED;

    case ACTION_FLAG_CEREMONY:
      if (is_press) {
        uint8_t cc, value;
        bool took_down_path = false;
        if (action_get_flag() == 1) {
          cc = action->params.flag_ceremony.flag_up_cc;
          value = action->params.flag_ceremony.flag_up_value;
          action_set_flag(0);
          ESP_LOGI(TAG, "Flag Ceremony: flag was UP, sending CC%d=%d, flag now DOWN",
            cc, value);
        } else {
          cc = action->params.flag_ceremony.flag_down_cc;
          value = action->params.flag_ceremony.flag_down_value;
          took_down_path = true;
          ESP_LOGI(TAG, "Flag Ceremony: flag was DOWN, sending CC%d=%d",
            cc, value);
        }
        send_control_change(channel, cc, value);
        action_set_cc_value(cc, value);

        if (took_down_path && config_get_flag_enabled() && action->raise_flag) {
          action_set_flag(1);
          ESP_LOGD(TAG, "Raise the Flag: flag set to 1 after Flag Ceremony (down path)");
        }
      }
      return ACTION_HANDLED_SKIP_FLAG;

    default:
      return ACTION_NOT_HANDLED;
  }
}
