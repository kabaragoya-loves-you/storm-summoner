#include "action_internal.h"
#include "device_config.h"
#include "param_stream.h"
#include "scene.h"
#include "transport.h"
#include "tempo.h"
#include "touchwheel_mode_mapping.h"
#include "ui.h"
#include "inspect_overlay.h"
#include "toast_overlay.h"
#include "midi_messages.h"
#include "assets_manager.h"
#include "cc_state.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "esp_log.h"
#include "esp_random.h"
#include <stdio.h>

static const char* TAG = "action_handlers_scene";

static uint16_t tempo_set_clamp_random_bound_x10(uint16_t v, uint16_t fallback) {
  if (v < TEMPO_MIN_BPM_X10 || v > TEMPO_MAX_BPM_X10) return fallback;
  return v;
}

static uint16_t tempo_set_resolve_bpm_x10(const action_t* action) {
  uint16_t configured = action->params.tempo.bpm;
  if (configured == ACTION_TEMPO_BPM_ORIGINAL) {
    scene_t* scene = scene_get_current();
    return scene ? scene->bpm_x10 : TEMPO_DEFAULT_BPM_X10;
  }
  if (configured == ACTION_TEMPO_BPM_RANDOM) {
    uint16_t lo = tempo_set_clamp_random_bound_x10(
      action->params.tempo.random_floor, TEMPO_MIN_BPM_X10);
    uint16_t hi = tempo_set_clamp_random_bound_x10(
      action->params.tempo.random_ceiling, TEMPO_MAX_BPM_X10);
    if (lo > hi) hi = lo;
    return (uint16_t)(lo + (esp_random() % (unsigned)(hi - lo + 1)));
  }
  return configured;
}

static void tempo_apply_bpm_x10(const action_t* action, uint16_t bpm_x10) {
  if (bpm_x10 < TEMPO_MIN_BPM_X10 || bpm_x10 > TEMPO_MAX_BPM_X10)
    return;
  if (action->morph_enabled) {
    uint32_t duration = action_morph_compute_duration_ms(action);
    action_tempo_morph_start(bpm_x10, duration);
  } else {
    tempo_set_bpm_x10(bpm_x10);
  }
}

// Apply a preset/program number, picking the right device_config call based
// on the active bank-select mode. Used by every "set preset" action variant.
static void apply_preset_program(uint16_t program) {
  if (device_config_get_bank_mode() != BANK_SELECT_NONE) {
    device_config_set_preset(program);
  } else {
    device_config_set_program(program & 0x7F);
  }
}

// Restore dirty CCs (and optional tempo/preset/screen/modulators) to the
// scene's stored defaults. Morphs the first MORPH_MAX_CCS dirty CCs when
// morph is enabled; anything past that is sent immediately.
static void restore_execute(const action_t* action) {
  scene_t* scene = scene_get_current();
  if (!scene || !action) return;

  action_clear_morphs();
  action_clear_boomerangs();

  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  uint8_t channel = scene_get_effective_channel(scene_index);
  channel = (channel >= 1) ? (uint8_t)(channel - 1) : 0;

  uint8_t cc_numbers[128];
  uint8_t target_values[128];
  uint8_t num_dirty = 0;

  for (int cc = 0; cc < 128; cc++) {
    if (!cc_state_is_dirty((uint8_t)cc)) continue;

    uint8_t target;
    if (scene->cc_defaults[cc] != SCENE_CC_DEFAULT_NONE) {
      target = scene->cc_defaults[cc];
    } else {
      const midi_control_t* ctrl = device ?
        assets_get_control_by_cc(device, (uint8_t)cc) : NULL;
      target = ctrl ? (uint8_t)(ctrl->min & 0x7F) : 0;
    }

    cc_numbers[num_dirty] = (uint8_t)cc;
    target_values[num_dirty] = target;
    num_dirty++;
  }

  if (num_dirty > 0) {
    uint8_t morph_ccs = (num_dirty > MORPH_MAX_CCS) ? MORPH_MAX_CCS : num_dirty;
    bool morphed = action_morph_start(action, morph_ccs, cc_numbers,
      target_values, true);

    uint8_t start_immediate = morphed ? morph_ccs : 0;
    for (uint8_t i = start_immediate; i < num_dirty; i++) {
      send_control_change(channel, cc_numbers[i], target_values[i]);
      action_set_cc_value(cc_numbers[i], target_values[i]);
      cc_state_clear_dirty(cc_numbers[i]);
    }

    if (morphed) {
      ESP_LOGI(TAG, "Restore: morphing %u CCs%s", (unsigned)morph_ccs,
        (num_dirty > morph_ccs) ? " (+immediate overflow)" : "");
    } else {
      ESP_LOGI(TAG, "Restore: sent %u CCs immediately", (unsigned)num_dirty);
    }
  }

  if (action->params.restore.restore_tempo)
    tempo_apply_bpm_x10(action, scene->bpm_x10);

  if (action->params.restore.restore_preset) {
    apply_preset_program(scene->program_number);
    ESP_LOGI(TAG, "Restore preset: %u", (unsigned)scene->program_number);
  }

  if (action->params.restore.restore_screen) {
    const char* name = scene->ui_module[0] ? scene->ui_module : "beat";
    ui_draw_module_t* mod = ui_get_module_by_name(name);
    if (mod) {
      ui_set_draw_module(mod);
      ESP_LOGI(TAG, "Restore screen: %s", name);
    }
  }

  if (action->params.restore.restore_modulators) {
    // Restore rate/shape/depth/etc. from the scene, but leave whatever is
    // currently running still running. *_apply_start_mode would re-apply the
    // scene's load-time start policy and can stop a live engine.
    bool lfo1_on = lfo_is_enabled(0);
    bool lfo2_on = lfo_is_enabled(1);
    lfo_config_t lfo1 = scene->lfo1_config;
    lfo_config_t lfo2 = scene->lfo2_config;
    lfo1.enabled = lfo1_on;
    lfo2.enabled = lfo2_on;
    lfo_apply_config(0, &lfo1);
    lfo_apply_config(1, &lfo2);

    bool rtg_running = rtg_is_running();
    rtg_config_t rtg_live;
    rtg_get_config(&rtg_live);
    rtg_config_t rtg = scene->rtg_config;
    rtg.enabled = rtg_live.enabled;
    rtg_apply_config(&rtg);
    if (rtg_running) rtg_start();
    else rtg_stop();

    bool sh_running = sample_hold_is_running();
    sample_hold_config_t sh_live;
    sample_hold_get_config(&sh_live);
    sample_hold_config_t sh = scene->sample_hold_config;
    sh.enabled = sh_live.enabled;
    sample_hold_apply_config(&sh);
    if (sh_running) sample_hold_start();
    else sample_hold_stop();

    ESP_LOGI(TAG, "Restore modulators (config only; run state preserved)");
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

  switch (action->type) {
    case ACTION_PRESET: {
      switch (action->variant) {
        case VARIANT_INCREMENT:
          if (is_press) device_config_program_next();
          break;

        case VARIANT_DECREMENT:
          if (is_press) device_config_program_prev();
          break;

        case VARIANT_SET:
          if (is_press) apply_preset_program(action->params.preset.program);
          break;

        case VARIANT_HOLD: {
          action_t* mutable_action = (action_t*)action;
          if (is_press) {
            action_followup_record_press(mutable_action);
            // Snapshot the live preset NOW so the release knows what to
            // restore. We capture even when release_to_original is false so
            // toggling the flag later (e.g. via console) does not strand a
            // hold action with a stale captured value -- cheap defensive
            // store.
            mutable_action->params.preset.captured_preset = device_config_get_preset();
          } else if (action_followup_should_skip_release(action)) {
            ESP_LOGD(TAG, "Preset hold release skipped by follow-up");
            break;
          }
          uint16_t program;
          if (is_press) {
            program = action->params.preset.press_preset;
          } else if (action->params.preset.release_to_original) {
            program = action->params.preset.captured_preset;
          } else {
            program = action->params.preset.release_preset;
          }
          apply_preset_program(program);
          ESP_LOGD(TAG, "Preset hold: %s -> %u%s",
            is_press ? "press" : "release",
            (unsigned)program,
            (!is_press && action->params.preset.release_to_original) ? " (original)" : "");
          break;
        }

        case VARIANT_CYCLE: {
          if (!is_press) break;
          action_t* mutable_action = (action_t*)action;
          uint8_t num_presets = mutable_action->params.preset.num_presets;
          if (num_presets == 0) {
            ESP_LOGW(TAG, "Preset cycle has no presets defined, skipping");
            break;
          }
          uint8_t idx = mutable_action->params.preset.current_index;
          uint16_t program = mutable_action->params.preset.cycle_presets[idx];

          apply_preset_program(program);
          ESP_LOGD(TAG, "Preset cycle step %u: preset %u", (unsigned)idx, (unsigned)program);

          mutable_action->params.preset.current_index = (idx + 1) % num_presets;
          break;
        }

        default:
          ESP_LOGW(TAG, "Unknown Preset variant %d", (int)action->variant);
          break;
      }
      return ACTION_HANDLED;
    }

    case ACTION_SCENE: {
      if (!is_press) return ACTION_HANDLED;

      switch (action->variant) {
        case VARIANT_INCREMENT:
          scene_next();
          break;
        case VARIANT_DECREMENT:
          scene_previous();
          break;
        case VARIANT_SET: {
          // target.number is a 0-based scene index everywhere else (menu seed,
          // picker write/read, JSON persistence). Summary display adds +1 for
          // human-readable text. Use it raw here.
          esp_err_t err = scene_set_current(action->params.target.number);
          if (err != ESP_OK) {
            ESP_LOGW(TAG, "Set Scene to %u failed: %s",
              (unsigned)action->params.target.number + 1, esp_err_to_name(err));
          }
          break;
        }
        default:
          ESP_LOGW(TAG, "Unknown Scene variant %d", (int)action->variant);
          break;
      }
      return ACTION_HANDLED;
    }

    case ACTION_TRANSPORT:
      // Press-only -- no transport operation has meaningful release semantics
      // today. Play/Record use transport-level toggle internally so we just
      // forward each variant to its dedicated entry point.
      if (!is_press) return ACTION_HANDLED;
      switch (action->variant) {
        case VARIANT_PLAY:   transport_play();   break;
        case VARIANT_STOP:   transport_stop();   break;
        case VARIANT_PAUSE:  transport_stop();   break;
        case VARIANT_RECORD: transport_record(); break;
        default:
          ESP_LOGW(TAG, "Unknown Transport variant %d", (int)action->variant);
          break;
      }
      return ACTION_HANDLED;

    case ACTION_TEMPO:
      switch (action->variant) {
        case VARIANT_TAP:
          if (is_press) {
            bool allow_frac = action->params.tempo.fractional != 0 ||
              tempo_get_allow_fractional_bpm();
            tempo_tap_ex(allow_frac);
          }
          return ACTION_HANDLED;

        case VARIANT_DOWNBEAT:
          if (is_press) tempo_resync_downbeat();
          return ACTION_HANDLED;

        case VARIANT_SET:
          if (is_press) {
            uint16_t bpm_x10 = tempo_set_resolve_bpm_x10(action);
            tempo_apply_bpm_x10(action, bpm_x10);
          }
          return ACTION_HANDLED;

        case VARIANT_INCREMENT:
          if (is_press) {
            uint16_t bpm_x10 = tempo_get_bpm_x10();
            uint16_t step_x10 = action->params.tempo.inc_amount;
            if (step_x10 == 0) step_x10 = 10;
            uint32_t target = (uint32_t)bpm_x10 + step_x10;
            if (target > TEMPO_MAX_BPM_X10) target = TEMPO_MAX_BPM_X10;
            if ((uint16_t)target != bpm_x10)
              tempo_apply_bpm_x10(action, (uint16_t)target);
          }
          return ACTION_HANDLED;

        case VARIANT_DECREMENT:
          if (is_press) {
            uint16_t bpm_x10 = tempo_get_bpm_x10();
            uint16_t step_x10 = action->params.tempo.inc_amount;
            if (step_x10 == 0) step_x10 = 10;
            int32_t target = (int32_t)bpm_x10 - (int32_t)step_x10;
            if (target < (int32_t)TEMPO_MIN_BPM_X10) target = TEMPO_MIN_BPM_X10;
            if ((uint16_t)target != bpm_x10)
              tempo_apply_bpm_x10(action, (uint16_t)target);
          }
          return ACTION_HANDLED;

        case VARIANT_HOLD: {
          if (is_press) {
            action_followup_record_press((action_t*)action);
          } else if (action_followup_should_skip_release(action)) {
            ESP_LOGD(TAG, "Tempo hold release skipped by follow-up");
            return ACTION_HANDLED;
          }
          uint16_t bpm_x10 = is_press ?
            action->params.tempo.press_bpm : action->params.tempo.release_bpm;
          tempo_apply_bpm_x10(action, bpm_x10);
          char bpm_buf[16];
          tempo_format_bpm(bpm_buf, sizeof(bpm_buf), bpm_x10);
          ESP_LOGD(TAG, "Tempo hold: %s -> %s BPM (%s)",
            is_press ? "press" : "release", bpm_buf,
            action->morph_enabled ? "morph" : "jump");
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
            uint16_t bpm_x10 = mutable_action->params.tempo.cycle_tempos[idx];

            tempo_apply_bpm_x10(action, bpm_x10);
            char bpm_buf[16];
            tempo_format_bpm(bpm_buf, sizeof(bpm_buf), bpm_x10);
            ESP_LOGD(TAG, "Tempo cycle step %u: %s BPM (%s)",
              (unsigned)idx, bpm_buf,
              action->morph_enabled ? "morph" : "jump");

            mutable_action->params.tempo.current_index = (idx + 1) % num_tempos;
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unhandled tempo variant: %d", action->variant);
          return ACTION_NOT_HANDLED;
      }

    case ACTION_CONFIRM_PENDING:
      if (is_press) {
        if (action->params.confirm.target == CONFIRM_TARGET_SCENE) {
          if (scene_has_pending_change()) scene_confirm_change();
        } else {
          if (device_config_has_pending_program()) device_config_confirm_program();
        }
      }
      return ACTION_HANDLED;

    case ACTION_TOUCHWHEEL:
      switch (action->variant) {
        case VARIANT_HOLD: {
          action_t* mutable_action = (action_t*)action;
          if (is_press) {
            action_followup_record_press(mutable_action);
            // Snapshot the live touchwheel mode NOW so the release knows
            // what to restore. We capture even when release_to_original is
            // false so toggling the flag later (e.g. via console) doesn't
            // strand a hold action with a stale captured value -- mirrors
            // the Preset Hold press-time capture pattern.
            mutable_action->params.tw_mode.captured_mode =
              touchwheel_get_current_mode_index(scene_get_current());
          } else if (action_followup_should_skip_release(action)) {
            ESP_LOGD(TAG, "Touchwheel hold release skipped by follow-up");
            return ACTION_HANDLED;
          }
          uint8_t user_mode_idx;
          if (is_press) {
            user_mode_idx = action->params.tw_mode.mode;
          } else if (action->params.tw_mode.release_to_original) {
            user_mode_idx = action->params.tw_mode.captured_mode;
          } else {
            user_mode_idx = action->params.tw_mode.mode2;
          }
          const touchwheel_mode_mapping_t* mapping = apply_touchwheel_mode_runtime(user_mode_idx);
          if (mapping)
            ESP_LOGD(TAG, "Touchwheel hold: %s -> %s%s",
              is_press ? "press" : "release",
              mapping->display_name,
              (!is_press && action->params.tw_mode.release_to_original) ? " (original)" : "");
          return ACTION_HANDLED;
        }

        case VARIANT_CYCLE:
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

        default:
          ESP_LOGW(TAG, "Unknown Touchwheel variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

    case ACTION_UI:
      switch (action->variant) {
        case VARIANT_SET:
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

        case VARIANT_HOLD:
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
          return ACTION_HANDLED;

        case VARIANT_CYCLE:
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

        default:
          ESP_LOGW(TAG, "Unknown UI variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

    case ACTION_PARAM:
      switch (action->variant) {
        case VARIANT_HOLD: {
          scene_t* scene = scene_get_current();
          if (!scene) return ACTION_HANDLED;
          action_t* mutable_action = (action_t*)action;
          param_target_t target = (param_target_t)action->params.tw_param.target;
          if (target >= PARAM_TARGET_COUNT) target = PARAM_TARGET_TOUCHWHEEL;
          if (!param_target_is_cc_active(scene, target)) return ACTION_HANDLED;

          uint8_t cc;
          uint8_t value;
          if (is_press) {
            param_target_capture(scene, target,
              &mutable_action->params.tw_param.captured_cc,
              &mutable_action->params.tw_param.captured_value);
            cc = action->params.tw_param.param;
            value = action_get_cc_value(cc);
          } else if (action->params.tw_param.release_to_original) {
            cc = action->params.tw_param.captured_cc;
            value = action->params.tw_param.captured_value;
          } else {
            cc = action->params.tw_param.param2;
            value = action_get_cc_value(cc);
          }
          param_target_apply(scene, target, cc, value);
          ESP_LOGI(TAG, "Param Hold [%s]: CC %u = %u (%s%s)",
            param_target_to_string(target), (unsigned)cc, (unsigned)value,
            is_press ? "press" : "release",
            (!is_press && action->params.tw_param.release_to_original) ? ", original" : "");
          return ACTION_HANDLED;
        }

        case VARIANT_CYCLE:
          if (is_press) {
            scene_t* scene = scene_get_current();
            if (scene) {
              action_t* mutable = (action_t*)action;
              param_target_t target = (param_target_t)mutable->params.tw_param.target;
              if (target >= PARAM_TARGET_COUNT) target = PARAM_TARGET_TOUCHWHEEL;
              if (!param_target_is_cc_active(scene, target)) return ACTION_HANDLED;

              uint8_t num = mutable->params.tw_param.num_params;
              if (num < 2) num = 2;
              uint8_t cc = mutable->params.tw_param.params[
                mutable->params.tw_param.current_index % num];
              uint8_t cached_value = action_get_cc_value(cc);
              param_target_apply(scene, target, cc, cached_value);
              ESP_LOGI(TAG, "Param Cycle [%s]: CC %u = %u (step %d/%d)",
                param_target_to_string(target), (unsigned)cc, (unsigned)cached_value,
                mutable->params.tw_param.current_index + 1, num);
              mutable->params.tw_param.current_index =
                (mutable->params.tw_param.current_index + 1) % num;
            }
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unknown Param variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

    case ACTION_INSPECT_SCENE:
      if (is_press) {
        inspect_overlay_show();
      } else {
        inspect_overlay_hide();
      }
      return ACTION_HANDLED;

    case ACTION_SNAPSHOT:
      if (is_press) {
        char name[17];
        esp_err_t err = scene_snapshot_current(name, sizeof(name));
        if (err == ESP_OK) {
          char msg[48];
          snprintf(msg, sizeof(msg), "Snapshot: %s", name);
          toast_overlay_show(msg);
        } else {
          toast_overlay_show("Snapshot failed");
          ESP_LOGW(TAG, "Snapshot failed: %s", esp_err_to_name(err));
        }
      }
      return ACTION_HANDLED;

    case ACTION_RESTORE:
      if (is_press) {
        restore_execute(action);
        toast_overlay_show("Restored");
      }
      return ACTION_HANDLED;

    default:
      return ACTION_NOT_HANDLED;
  }
}
