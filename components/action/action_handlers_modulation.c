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
#include "esp_random.h"

static const char* TAG = "action_handlers_modulation";

static uint8_t lfo_modify_resolve_u8_rand(uint8_t v, uint8_t lo, uint8_t hi) {
  if (v != ACTION_LFO_RAND_U8) return v;
  return lo + (uint8_t)(esp_random() % (unsigned)(hi - lo + 1));
}

static uint8_t lfo_modify_resolve_u8_table(uint8_t v, const uint8_t* tbl, size_t n) {
  if (v != ACTION_LFO_RAND_U8) return v;
  return tbl[esp_random() % n];
}

static uint16_t lfo_modify_resolve_u16_table(uint16_t v, const uint16_t* tbl, size_t n) {
  if (v != ACTION_LFO_RAND_U16) return v;
  return tbl[esp_random() % n];
}

static uint8_t lfo_modify_resolve_steps(uint8_t v) {
  static const uint8_t steps[] = { 16, 32, 64, 128 };
  if (v != ACTION_LFO_RAND_STEPS) return v;
  return steps[esp_random() % (sizeof(steps) / sizeof(steps[0]))];
}

// ============================================================================
// Per-slot LFO helpers
// ----------------------------------------------------------------------------
// LFO_START / LFO_STOP / LFO_TOGGLE all branch on slot 1/2/3 with mirrored
// behavior per slot. These helpers run the operation on one engine index
// (0 = LFO1, 1 = LFO2) and reflect the new enabled state in the scene's
// matching continuous_mapping (NULL skips the scene write).
// ============================================================================

static void lfo_start_one(uint8_t lfo_index, continuous_mapping_t* scene_mapping) {
  lfo_trigger_start(lfo_index);
  if (scene_mapping) scene_mapping->enabled = true;
}

static void lfo_stop_one(uint8_t lfo_index, continuous_mapping_t* scene_mapping) {
  if (lfo_is_enabled(lfo_index)) {
    if (lfo_get_restore_on_stop(lfo_index)) {
      midi_lfo_scene_handler_restore_value(lfo_index);
    }
    // Release any held NOTE-output mapping voice before stopping the LFO
    // loop, so the channel computation still has the same scene context the
    // NoteOn used.
    midi_lfo_scene_handler_release_notes_for_slot(lfo_index);
    lfo_enable(lfo_index, false);
    if (scene_mapping) scene_mapping->enabled = false;
  } else if (lfo_is_pending_start(lfo_index)) {
    lfo_enable(lfo_index, false);
  }
}

static void lfo_toggle_one(uint8_t lfo_index, continuous_mapping_t* scene_mapping) {
  bool new_state = !lfo_is_enabled(lfo_index);
  if (!new_state) {
    if (lfo_get_restore_on_stop(lfo_index)) {
      midi_lfo_scene_handler_restore_value(lfo_index);
    }
    midi_lfo_scene_handler_release_notes_for_slot(lfo_index);
  }
  lfo_enable(lfo_index, new_state);
  if (scene_mapping) scene_mapping->enabled = new_state;
}

action_handle_result_t action_handlers_modulation_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;

  switch (action->type) {
    case ACTION_LFO: {
      if (!is_press) return ACTION_HANDLED;
      uint8_t slot = action->params.lfo.slot;
      scene_t* scene = scene_get_current();
      switch (action->variant) {
        case VARIANT_START:
          if (slot == 1 || slot == 3) lfo_start_one(0, scene ? &scene->lfo1 : NULL);
          if (slot == 2 || slot == 3) lfo_start_one(1, scene ? &scene->lfo2 : NULL);
          ESP_LOGI(TAG, "LFO Start: slot %d", slot);
          return ACTION_HANDLED;
        case VARIANT_STOP:
          if (slot == 1 || slot == 3) lfo_stop_one(0, scene ? &scene->lfo1 : NULL);
          if (slot == 2 || slot == 3) lfo_stop_one(1, scene ? &scene->lfo2 : NULL);
          ESP_LOGI(TAG, "LFO Stop: slot %d", slot);
          return ACTION_HANDLED;
        case VARIANT_TOGGLE:
          if (slot == 1 || slot == 3) lfo_toggle_one(0, scene ? &scene->lfo1 : NULL);
          if (slot == 2 || slot == 3) lfo_toggle_one(1, scene ? &scene->lfo2 : NULL);
          ESP_LOGI(TAG, "LFO Toggle: slot %d", slot);
          return ACTION_HANDLED;
        case VARIANT_MODIFY: {
          // Apply each non-sentinel override in place. Mirrors the SHAPE
          // action's "runtime mutation without phase reset" behavior, but
          // generalized to every LFO parameter. Polarity is on the per-scene
          // continuous_mapping (not the engine), so we mutate the scene
          // struct directly for that field.
          const action_t* a = action;
          int applied = 0;
          for (int side = 0; side < 2; side++) {
            uint8_t lfo_index = (uint8_t)side;
            uint8_t slot_bit = (side == 0) ? 1 : 2;
            if (!(slot == slot_bit || slot == 3)) continue;

            continuous_mapping_t* scene_mapping =
              scene ? (lfo_index == 0 ? &scene->lfo1 : &scene->lfo2) : NULL;

            if (a->params.lfo.waveform != ACTION_LFO_ORIG_U8) {
              static const uint8_t waveforms[] = {
                LFO_WAVEFORM_SINE, LFO_WAVEFORM_TRIANGLE, LFO_WAVEFORM_SQUARE,
                LFO_WAVEFORM_SAW_UP, LFO_WAVEFORM_SAW_DOWN, LFO_WAVEFORM_SAMPLE_HOLD,
              };
              uint8_t wf = lfo_modify_resolve_u8_table(a->params.lfo.waveform,
                waveforms, sizeof(waveforms) / sizeof(waveforms[0]));
              lfo_set_waveform(lfo_index, (lfo_waveform_t)wf);
              applied++;
            }
            if (a->params.lfo.rate_mode != ACTION_LFO_ORIG_U8) {
              static const uint8_t rate_modes[] = {
                LFO_RATE_MODE_FREE, LFO_RATE_MODE_TEMPO, LFO_RATE_MODE_TOUCHWHEEL,
                LFO_RATE_MODE_EXPRESSION, LFO_RATE_MODE_CV, LFO_RATE_MODE_ALS,
                LFO_RATE_MODE_PROXIMITY,
              };
              uint8_t rm = lfo_modify_resolve_u8_table(a->params.lfo.rate_mode,
                rate_modes, sizeof(rate_modes) / sizeof(rate_modes[0]));
              lfo_set_rate_mode(lfo_index, (lfo_rate_mode_t)rm);
              applied++;
            }
            if (a->params.lfo.rate_hz_x100 != ACTION_LFO_ORIG_U16) {
              static const uint16_t rates_x100[] = {
                5, 10, 25, 50, 100, 200, 300, 500, 800, 1000, 1500, 2000,
              };
              uint16_t hz_x100 = lfo_modify_resolve_u16_table(a->params.lfo.rate_hz_x100,
                rates_x100, sizeof(rates_x100) / sizeof(rates_x100[0]));
              lfo_set_rate_hz(lfo_index, (float)hz_x100 / 100.0f);
              applied++;
            }
            if (a->params.lfo.division != ACTION_LFO_ORIG_U8) {
              static const uint8_t divisions[] = {
                LFO_DIVISION_16_BARS, LFO_DIVISION_12_BARS, LFO_DIVISION_8_BARS,
                LFO_DIVISION_4_BARS, LFO_DIVISION_2_BARS, LFO_DIVISION_1_BAR,
                LFO_DIVISION_HALF, LFO_DIVISION_QUARTER, LFO_DIVISION_EIGHTH,
                LFO_DIVISION_SIXTEENTH, LFO_DIVISION_32ND,
              };
              uint8_t div = lfo_modify_resolve_u8_table(a->params.lfo.division,
                divisions, sizeof(divisions) / sizeof(divisions[0]));
              lfo_set_division(lfo_index, (lfo_note_division_t)div);
              applied++;
            }
            if (a->params.lfo.polarity != ACTION_LFO_ORIG_U8 && scene_mapping) {
              static const uint8_t polarities[] = {
                POLARITY_UNIPOLAR, POLARITY_BIPOLAR, POLARITY_INVERTED,
              };
              uint8_t pol = lfo_modify_resolve_u8_table(a->params.lfo.polarity,
                polarities, sizeof(polarities) / sizeof(polarities[0]));
              scene_mapping->polarity = (polarity_t)pol;
              applied++;
            }
            if (a->params.lfo.floor != ACTION_LFO_ORIG_U8) {
              uint8_t floor = lfo_modify_resolve_u8_rand(a->params.lfo.floor, 0, 127);
              lfo_set_floor(lfo_index, floor);
              applied++;
            }
            if (a->params.lfo.ceiling != ACTION_LFO_ORIG_U8) {
              uint8_t ceiling = lfo_modify_resolve_u8_rand(a->params.lfo.ceiling, 0, 127);
              lfo_set_ceiling(lfo_index, ceiling);
              applied++;
            }
            if (a->params.lfo.resolution_mode != ACTION_LFO_ORIG_U8) {
              static const uint8_t resolutions[] = {
                LFO_RESOLUTION_AUTO, LFO_RESOLUTION_COARSE, LFO_RESOLUTION_MEDIUM,
                LFO_RESOLUTION_FINE, LFO_RESOLUTION_MANUAL,
              };
              uint8_t res = lfo_modify_resolve_u8_table(a->params.lfo.resolution_mode,
                resolutions, sizeof(resolutions) / sizeof(resolutions[0]));
              lfo_set_resolution_mode(lfo_index, (lfo_resolution_mode_t)res);
              applied++;
            }
            if (a->params.lfo.manual_steps != ACTION_LFO_ORIG_STEPS) {
              uint8_t steps = lfo_modify_resolve_steps(a->params.lfo.manual_steps);
              lfo_set_manual_steps(lfo_index, steps);
              applied++;
            }
          }
          ESP_LOGI(TAG, "LFO Modify: slot %d, %d override(s) applied", slot, applied);
          return ACTION_HANDLED;
        }
        default:
          ESP_LOGW(TAG, "Unknown LFO variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }
    }

    case ACTION_CLOCK: {
      switch (action->variant) {
        case VARIANT_TOGGLE:
          if (is_press) {
            scene_t* scene = scene_get_current();
            if (scene) {
              scene->send_clock = !scene->send_clock;
              ESP_LOGI(TAG, "Clock Toggle: send_clock now %s",
                scene->send_clock ? "enabled" : "disabled");
            }
          }
          return ACTION_HANDLED;
        case VARIANT_HOLD: {
          if (is_press) {
            action_followup_record_press((action_t*)action);
          } else if (action_followup_should_skip_release(action)) {
            ESP_LOGD(TAG, "Clock hold release skipped by follow-up");
            return ACTION_HANDLED;
          }
          scene_t* scene = scene_get_current();
          if (scene) {
            bool press_state = action->params.clock.start_enabled;
            scene->send_clock = is_press ? press_state : !press_state;
            ESP_LOGI(TAG, "Clock Hold: send_clock now %s",
              scene->send_clock ? "enabled" : "disabled");
          }
          return ACTION_HANDLED;
        }
        case VARIANT_BURST:
          if (is_press) {
            action_clock_burst_start((uint8_t)action->params.clock.speed_percent);
          } else {
            action_clock_burst_stop();
          }
          return ACTION_HANDLED;
        default:
          ESP_LOGW(TAG, "Unknown Clock variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }
    }

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
      if (is_press) {
        action_followup_record_press((action_t*)action);
      } else if (action_followup_should_skip_release(action)) {
        ESP_LOGD(TAG, "Cut hold release skipped by follow-up");
        return ACTION_HANDLED;
      }
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
        action_followup_record_press((action_t*)action);
        rtg_start();
        ESP_LOGD(TAG, "RTG Hold: press -> running");
      } else if (action_followup_should_skip_release(action)) {
        ESP_LOGD(TAG, "RTG hold release skipped by follow-up");
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
        action_followup_record_press((action_t*)action);
        sample_hold_start();
        ESP_LOGD(TAG, "S+H Hold: press -> running");
      } else if (action_followup_should_skip_release(action)) {
        ESP_LOGD(TAG, "S+H hold release skipped by follow-up");
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
