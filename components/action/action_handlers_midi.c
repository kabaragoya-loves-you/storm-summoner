#include "action_internal.h"
#include "midi_messages.h"
#include "device_config.h"
#include "scene.h"
#include "assets_manager.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "action_handlers_midi";

action_handle_result_t action_handlers_midi_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;

  switch (action->type) {
    case ACTION_CONTROL_CHANGE:
      if (is_press) {
        uint8_t num_ccs = action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;
        for (int i = 0; i < num_ccs && i < 4; i++) {
          uint8_t cc = action->params.control.cc_numbers[i];
          uint8_t value = action->params.control.values[i];
          send_control_change(channel, cc, value);
          action_set_cc_value(cc, value);
          ESP_LOGD(TAG, "Sent CC%d=%d", cc, value);
        }
      }
      return ACTION_HANDLED;

    case ACTION_CONTROL_HOLD: {
      action_t* mutable_action = (action_t*)action;
      uint8_t num_ccs = action->params.control.num_ccs;
      if (num_ccs == 0) num_ccs = 1;

      if (is_press) {
        mutable_action->params.control.press_time_us = esp_timer_get_time();
      } else {
        uint8_t mode = action->params.control.release_mode;
        if (mode != 0) {
          int64_t elapsed_ms = (esp_timer_get_time() -
            mutable_action->params.control.press_time_us) / 1000;
          uint16_t threshold = action->params.control.release_threshold_ms;
          if (threshold == 0) threshold = 1000;

          bool should_send = (mode == 1) ? (elapsed_ms >= threshold)
                                         : (elapsed_ms < threshold);
          if (!should_send) {
            ESP_LOGD(TAG, "CC Hold release skipped: mode=%d, elapsed=%lld, thresh=%u",
              mode, (long long)elapsed_ms, (unsigned)threshold);
            return ACTION_HANDLED;
          }
        }
      }

      uint8_t target_values[4];
      for (int i = 0; i < num_ccs && i < 4; i++) {
        target_values[i] = is_press ?
          action->params.control.values[i] : action->params.control.values2[i];
      }

      if (action->morph_enabled) {
        if (action_morph_start(action, num_ccs, action->params.control.cc_numbers, target_values)) {
          ESP_LOGD(TAG, "CC%d hold morph started -> %d",
            action->params.control.cc_numbers[0], target_values[0]);
          return ACTION_HANDLED;
        }
      }

      for (int i = 0; i < num_ccs && i < 4; i++) {
        send_control_change(channel, action->params.control.cc_numbers[i], target_values[i]);
        action_set_cc_value(action->params.control.cc_numbers[i], target_values[i]);
        ESP_LOGD(TAG, "CC%d hold: %d", action->params.control.cc_numbers[i], target_values[i]);
      }
      return ACTION_HANDLED;
    }

    case ACTION_CONTROL_CYCLE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_steps = mutable_action->params.control.num_cycle_steps;
        if (num_steps == 0) {
          ESP_LOGW(TAG, "Control cycle has no steps defined, skipping");
          return ACTION_HANDLED;
        }
        uint8_t idx = mutable_action->params.control.current_index;
        ESP_LOGI(TAG, "Control Cycle executing: idx=%d, num_steps=%d", idx, num_steps);
        uint8_t num_ccs = mutable_action->params.control.num_ccs;
        if (num_ccs == 0) num_ccs = 1;

        uint8_t target_values[4];
        for (int i = 0; i < num_ccs && i < 4; i++) {
          target_values[i] = mutable_action->params.control.cycle_values[i][idx];
        }

        if (action->morph_enabled) {
          if (action_morph_start(action, num_ccs, mutable_action->params.control.cc_numbers,
              target_values)) {
            ESP_LOGD(TAG, "CC%d cycle morph started -> %d",
              mutable_action->params.control.cc_numbers[0], target_values[0]);
            mutable_action->params.control.current_index = (idx + 1) % num_steps;
            return ACTION_HANDLED;
          }
        }

        for (int i = 0; i < num_ccs && i < 4; i++) {
          send_control_change(channel, mutable_action->params.control.cc_numbers[i],
            target_values[i]);
          action_set_cc_value(mutable_action->params.control.cc_numbers[i], target_values[i]);
          ESP_LOGD(TAG, "Cycled CC%d to %d", mutable_action->params.control.cc_numbers[i],
            target_values[i]);
        }

        mutable_action->params.control.current_index = (idx + 1) % num_steps;
      }
      return ACTION_HANDLED;

    case ACTION_NOTE:
      if (is_press) {
        send_note_on(channel, action->params.note.note, action->params.note.velocity);
        ESP_LOGD(TAG, "Note On: %d vel=%d", action->params.note.note, action->params.note.velocity);
      } else {
        send_note_off(channel, action->params.note.note, 0);
        ESP_LOGD(TAG, "Note Off: %d", action->params.note.note);
      }
      return ACTION_HANDLED;

    case ACTION_RANDOMIZE:
      if (is_press) {
        uint8_t scene_index = scene_get_current_index();
        const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);

        uint8_t num_ccs = action->params.randomize.num_ccs;
        if (num_ccs > 8) num_ccs = 8;

        uint8_t target_values[8];
        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          uint8_t random_val;

          const midi_control_t* ctrl = device ?
            assets_get_control_by_cc(device, cc) : NULL;

          if (ctrl && ctrl->discrete_count > 0 && ctrl->discrete_values) {
            uint8_t idx = esp_random() % ctrl->discrete_count;
            random_val = ctrl->discrete_values[idx].value;
          } else if (ctrl) {
            uint8_t range = ctrl->max - ctrl->min + 1;
            random_val = ctrl->min + (esp_random() % range);
          } else {
            random_val = esp_random() % 128;
          }

          target_values[i] = random_val;
        }

        if (action->morph_enabled && num_ccs > 0) {
          uint8_t morph_ccs = (num_ccs > 4) ? 4 : num_ccs;
          if (action_morph_start(action, morph_ccs, action->params.randomize.cc_numbers,
              target_values)) {
            ESP_LOGD(TAG, "Randomize morph started for %d CCs", morph_ccs);

            for (int i = 4; i < num_ccs; i++) {
              uint8_t cc = action->params.randomize.cc_numbers[i];
              send_control_change(channel, cc, target_values[i]);
              action_set_cc_value(cc, target_values[i]);
              ESP_LOGD(TAG, "Randomized CC%d to %d (immediate)", cc, target_values[i]);
            }
            return ACTION_HANDLED;
          }
        }

        for (int i = 0; i < num_ccs; i++) {
          uint8_t cc = action->params.randomize.cc_numbers[i];
          send_control_change(channel, cc, target_values[i]);
          action_set_cc_value(cc, target_values[i]);
          ESP_LOGD(TAG, "Randomized CC%d to %d", cc, target_values[i]);
        }
        ESP_LOGD(TAG, "Randomized %d CCs", num_ccs);
      }
      return ACTION_HANDLED;

    case ACTION_RESET:
      if (is_press) {
        send_control_change(channel, 123, 0);
        send_control_change(channel, 120, 0);
        send_reset();
        ESP_LOGD(TAG, "Sent Reset (CC123 + CC120 + 0xFF)");
      }
      return ACTION_HANDLED;

    case ACTION_SUSTAIN:
      send_control_change(channel, 64, is_press ? 127 : 0);
      ESP_LOGD(TAG, "Sustain: %s", is_press ? "on" : "off");
      return ACTION_HANDLED;

    case ACTION_SOSTENUTO:
      send_control_change(channel, 66, is_press ? 127 : 0);
      ESP_LOGD(TAG, "Sostenuto: %s", is_press ? "on" : "off");
      return ACTION_HANDLED;

    default:
      return ACTION_NOT_HANDLED;
  }
}
