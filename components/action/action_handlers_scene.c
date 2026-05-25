#include "action_internal.h"
#include "device_config.h"
#include "scene.h"
#include "transport.h"
#include "tempo.h"
#include "touchwheel_mode_mapping.h"
#include "ui.h"
#include "esp_log.h"

static const char* TAG = "action_handlers_scene";

// Apply a preset/program number, picking the right device_config call based
// on the active bank-select mode. Used by every "set preset" action variant.
static void apply_preset_program(uint16_t program) {
  if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
    device_config_set_preset(program);
  } else {
    device_config_set_program(program & 0x7F);
  }
}

// Switch the current scene's touchwheel runtime to the given user-mode index.
// Mirrors the scene mutation used by both TOUCHWHEEL_HOLD and TOUCHWHEEL_CYCLE
// so the two action types stay in sync when fields are added to the mapping.
// Returns the resolved mapping (or NULL if user_mode_idx is invalid) so callers
// can log a display name without re-resolving.
static const touchwheel_mode_mapping_t* apply_touchwheel_mode_runtime(uint8_t user_mode_idx) {
  const touchwheel_mode_mapping_t* mapping = touchwheel_get_mode_mapping(user_mode_idx);
  if (!mapping) return NULL;

  uint8_t scene_index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  if (scene) {
    scene->touchwheel_style = mapping->default_style;
    scene->touchwheel.enabled = (mapping->mode != TOUCHWHEEL_MODE_PADS);
    if (mapping->use_output_type) {
      scene->touchwheel.output_type = mapping->output_type;
      if (mapping->output_type == OUTPUT_TYPE_NOTE) {
        if (scene->touchwheel.base_note == 0) scene->touchwheel.base_note = 60;
        if (scene->touchwheel.note_range == 0) scene->touchwheel.note_range = 24;
        if (scene->touchwheel.velocity == 0) scene->touchwheel.velocity = 100;
      }
    }
  }
  scene_set_touchwheel_mode_runtime(scene_index, mapping->mode);
  return mapping;
}

action_handle_result_t action_handlers_scene_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;
  (void)channel;

  scene_mode_t current_mode = scene_get_mode();

  switch (action->type) {
    case ACTION_PRESET_INC:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset +1 action ignored: not allowed in Preset Sync mode");
        return ACTION_HANDLED;
      }
      if (is_press) device_config_program_next();
      return ACTION_HANDLED;

    case ACTION_PRESET_DEC:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset -1 action ignored: not allowed in Preset Sync mode");
        return ACTION_HANDLED;
      }
      if (is_press) device_config_program_prev();
      return ACTION_HANDLED;

    case ACTION_PRESET:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Set Preset action ignored: not allowed in Preset Sync mode");
        return ACTION_HANDLED;
      }
      if (is_press) apply_preset_program(action->params.preset.program);
      return ACTION_HANDLED;

    case ACTION_PRESET_HOLD: {
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset Hold action ignored: not allowed in Preset Sync mode");
        return ACTION_HANDLED;
      }
      uint16_t program = is_press ?
        action->params.preset_cycle.press_preset :
        action->params.preset_cycle.release_preset;
      apply_preset_program(program);
      ESP_LOGD(TAG, "Preset hold: %s -> %u", is_press ? "press" : "release",
        (unsigned)program);
      return ACTION_HANDLED;
    }

    case ACTION_PRESET_CYCLE:
      if (current_mode == SCENE_MODE_PRESET_SYNC) {
        ESP_LOGW(TAG, "Preset Cycle action ignored: not allowed in Preset Sync mode");
        return ACTION_HANDLED;
      }
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t num_presets = mutable_action->params.preset_cycle.num_presets;
        if (num_presets == 0) {
          ESP_LOGW(TAG, "Preset cycle has no presets defined, skipping");
          return ACTION_HANDLED;
        }
        uint8_t idx = mutable_action->params.preset_cycle.current_index;
        uint16_t program = mutable_action->params.preset_cycle.cycle_presets[idx];

        apply_preset_program(program);
        ESP_LOGD(TAG, "Preset cycle step %u: preset %u", (unsigned)idx, (unsigned)program);

        mutable_action->params.preset_cycle.current_index = (idx + 1) % num_presets;
      }
      return ACTION_HANDLED;

    case ACTION_SCENE_INC:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Scene +1 action ignored: not allowed in Simple mode");
        return ACTION_HANDLED;
      }
      if (is_press) scene_next();
      return ACTION_HANDLED;

    case ACTION_SCENE_DEC:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Scene -1 action ignored: not allowed in Simple mode");
        return ACTION_HANDLED;
      }
      if (is_press) scene_previous();
      return ACTION_HANDLED;

    case ACTION_SCENE:
      if (current_mode == SCENE_MODE_SINGLE) {
        ESP_LOGW(TAG, "Set Scene action ignored: not allowed in Simple mode");
        return ACTION_HANDLED;
      }
      if (is_press) {
        // target.number is a 0-based scene index everywhere else (menu seed,
        // picker write/read, JSON persistence). Summary display adds +1 for
        // human-readable text. Use it raw here.
        esp_err_t err = scene_set_current(action->params.target.number);
        if (err != ESP_OK) {
          ESP_LOGW(TAG, "Set Scene to %u failed: %s",
            (unsigned)action->params.target.number + 1, esp_err_to_name(err));
        }
      }
      return ACTION_HANDLED;

    case ACTION_PLAY:
      if (is_press) transport_play();
      return ACTION_HANDLED;

    case ACTION_STOP:
      if (is_press) transport_stop();
      return ACTION_HANDLED;

    case ACTION_PAUSE:
      if (is_press) transport_pause();
      return ACTION_HANDLED;

    case ACTION_RECORD:
      if (is_press) transport_record();
      return ACTION_HANDLED;

    case ACTION_TEMPO:
      switch (action->variant) {
        case VARIANT_TAP:
          if (is_press) tempo_tap();
          return ACTION_HANDLED;

        case VARIANT_SET:
          if (is_press && action->params.tempo.bpm > 0) {
            tempo_set_bpm(action->params.tempo.bpm);
          }
          return ACTION_HANDLED;

        case VARIANT_INCREMENT:
          if (is_press) {
            uint16_t bpm = tempo_get_bpm();
            uint8_t amount = action->params.tempo.inc_amount;
            if (amount == 0) amount = 1;
            uint16_t target = (bpm + amount > 300) ? 300 : (uint16_t)(bpm + amount);
            if (target < 20) target = 20;
            if (target != bpm) tempo_set_bpm(target);
          }
          return ACTION_HANDLED;

        case VARIANT_DECREMENT:
          if (is_press) {
            uint16_t bpm = tempo_get_bpm();
            uint8_t amount = action->params.tempo.inc_amount;
            if (amount == 0) amount = 1;
            uint16_t target = (bpm <= 20 + amount) ? 20 : (uint16_t)(bpm - amount);
            if (target > 300) target = 300;
            if (target != bpm) tempo_set_bpm(target);
          }
          return ACTION_HANDLED;

        case VARIANT_HOLD: {
          uint16_t bpm = is_press ?
            action->params.tempo.press_bpm : action->params.tempo.release_bpm;
          if (bpm >= 20 && bpm <= 300) {
            tempo_set_bpm(bpm);
            ESP_LOGD(TAG, "Tempo hold: %s -> %u BPM", is_press ? "press" : "release",
              (unsigned)bpm);
          }
          return ACTION_HANDLED;
        }

        case VARIANT_CYCLE:
          if (is_press) {
            action_t* mutable_action = (action_t*)action;
            uint8_t num_tempos = mutable_action->params.tempo.num_tempos;
            if (num_tempos == 0) {
              ESP_LOGW(TAG, "Tempo cycle has no tempos defined, skipping");
              return ACTION_HANDLED;
            }
            uint8_t idx = mutable_action->params.tempo.current_index;
            uint16_t bpm = mutable_action->params.tempo.cycle_tempos[idx];

            if (bpm >= 20 && bpm <= 300) {
              tempo_set_bpm(bpm);
              ESP_LOGD(TAG, "Tempo cycle step %u: %u BPM", (unsigned)idx, (unsigned)bpm);
            }

            mutable_action->params.tempo.current_index = (idx + 1) % num_tempos;
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unhandled tempo variant: %d", action->variant);
          return ACTION_NOT_HANDLED;
      }

    case ACTION_CONFIRM_PENDING:
      if (is_press) {
        scene_mode_t mode = scene_get_mode();
        if (mode == SCENE_MODE_SINGLE) {
          if (device_config_has_pending_program()) device_config_confirm_program();
        } else if (mode == SCENE_MODE_PRESET_SYNC) {
          if (scene_has_pending_change()) scene_confirm_change();
        } else {
          if (action->params.confirm.target == CONFIRM_TARGET_SCENE) {
            if (scene_has_pending_change()) scene_confirm_change();
          } else {
            if (device_config_has_pending_program()) device_config_confirm_program();
          }
        }
      }
      return ACTION_HANDLED;

    case ACTION_TOUCHWHEEL_HOLD: {
      uint8_t user_mode_idx = is_press ?
        action->params.tw_mode.mode : action->params.tw_mode.mode2;
      const touchwheel_mode_mapping_t* mapping = apply_touchwheel_mode_runtime(user_mode_idx);
      if (mapping)
        ESP_LOGD(TAG, "Touchwheel mode hold: %s", mapping->display_name);
      return ACTION_HANDLED;
    }

    case ACTION_TOUCHWHEEL_CYCLE:
      if (is_press) {
        action_t* mutable_action = (action_t*)action;
        uint8_t idx = mutable_action->params.tw_mode.current_index;
        uint8_t user_mode_idx = mutable_action->params.tw_mode.modes[idx];
        const touchwheel_mode_mapping_t* mapping = apply_touchwheel_mode_runtime(user_mode_idx);
        if (mapping)
          ESP_LOGD(TAG, "Cycled touchwheel mode to %s", mapping->display_name);

        mutable_action->params.tw_mode.current_index =
          (idx + 1) % mutable_action->params.tw_mode.num_modes;
      }
      return ACTION_HANDLED;

    case ACTION_SET_UI:
      if (is_press) {
        uint8_t idx = action->params.ui.module;
        if (idx < ui_scene_selectable_module_count) {
          ui_draw_module_t* mod = ui_get_module_by_name(
            ui_scene_selectable_modules[idx]);
          if (mod) {
            ui_set_draw_module(mod);
            ESP_LOGI(TAG, "Set UI: %s", ui_scene_selectable_modules[idx]);
          }
        }
      }
      return ACTION_HANDLED;

    case ACTION_UI_HOLD: {
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
      return ACTION_HANDLED;
    }

    case ACTION_UI_CYCLE:
      if (is_press) {
        action_t* mutable = (action_t*)action;
        uint8_t num = mutable->params.ui.num_modules;
        if (num < 2) num = 2;
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
      return ACTION_HANDLED;

    case ACTION_PARAM_HOLD: {
      scene_t* scene = scene_get_current();
      if (scene) {
        uint8_t cc = is_press
          ? action->params.tw_param.param
          : action->params.tw_param.param2;
        scene->touchwheel.cc_numbers[0] = cc;
        uint8_t cached_value = action_get_cc_value(cc);
        scene_set_touchwheel_value(cached_value);
        ESP_LOGI(TAG, "Param Hold: CC %u = %u (%s)",
          (unsigned)cc, (unsigned)cached_value,
          is_press ? "press" : "release");
      }
      return ACTION_HANDLED;
    }

    case ACTION_PARAM_CYCLE:
      if (is_press) {
        scene_t* scene = scene_get_current();
        if (scene) {
          action_t* mutable = (action_t*)action;
          uint8_t num = mutable->params.tw_param.num_params;
          if (num < 2) num = 2;
          uint8_t cc = mutable->params.tw_param.params[
            mutable->params.tw_param.current_index % num];
          scene->touchwheel.cc_numbers[0] = cc;
          uint8_t cached_value = action_get_cc_value(cc);
          scene_set_touchwheel_value(cached_value);
          ESP_LOGI(TAG, "Param Cycle: CC %u = %u (step %d/%d)",
            (unsigned)cc, (unsigned)cached_value,
            mutable->params.tw_param.current_index + 1, num);
          mutable->params.tw_param.current_index =
            (mutable->params.tw_param.current_index + 1) % num;
        }
      }
      return ACTION_HANDLED;

    default:
      return ACTION_NOT_HANDLED;
  }
}
