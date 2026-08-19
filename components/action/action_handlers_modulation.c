#include "action_internal.h"
#include "midi_messages.h"
#include "midi_out.h"
#include "scene.h"
#include "config.h"
#include "lfo.h"
#include "rtg.h"
#include "sample_hold.h"
#include "stream.h"
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

static int apply_engine_modify(const action_engine_modify_t* m, bool rtg) {
  static const uint8_t rate_modes[] = { 0, 1 };
  static const uint16_t rates_x100[] = {
    50, 75, 100, 125, 150, 175, 200, 250, 300, 350, 400, 500,
    600, 700, 800, 900, 1000, 1250, 1500, 1750, 2000, 2500,
  };
  static const uint8_t divisions[] = {
    LFO_DIVISION_16_BARS, LFO_DIVISION_12_BARS, LFO_DIVISION_8_BARS,
    LFO_DIVISION_4_BARS, LFO_DIVISION_2_BARS, LFO_DIVISION_1_BAR,
    LFO_DIVISION_HALF, LFO_DIVISION_QUARTER, LFO_DIVISION_EIGHTH,
    LFO_DIVISION_SIXTEENTH, LFO_DIVISION_32ND,
  };
  static const uint8_t prob_values[] = {
    10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
  };
  int applied = 0;

  if (m->rate_mode != ACTION_LFO_ORIG_U8) {
    uint8_t rm = lfo_modify_resolve_u8_table(m->rate_mode, rate_modes,
      sizeof(rate_modes) / sizeof(rate_modes[0]));
    if (rtg) rtg_set_rate_mode((rtg_rate_mode_t)rm);
    else sample_hold_set_rate_mode((sample_hold_rate_mode_t)rm);
    applied++;
  }
  if (m->rate_hz_x100 != ACTION_LFO_ORIG_U16) {
    uint16_t hz_x100 = lfo_modify_resolve_u16_table(m->rate_hz_x100, rates_x100,
      sizeof(rates_x100) / sizeof(rates_x100[0]));
    if (rtg) rtg_set_rate_hz((float)hz_x100 / 100.0f);
    else sample_hold_set_rate_hz((float)hz_x100 / 100.0f);
    applied++;
  }
  if (m->division != ACTION_LFO_ORIG_U8) {
    uint8_t div = lfo_modify_resolve_u8_table(m->division, divisions,
      sizeof(divisions) / sizeof(divisions[0]));
    if (rtg) rtg_set_division((lfo_note_division_t)div);
    else sample_hold_set_division((lfo_note_division_t)div);
    applied++;
  }
  if (m->glide != ACTION_LFO_ORIG_U8) {
    bool glide = (m->glide != 0);
    if (rtg) rtg_set_glide(glide);
    else sample_hold_set_glide(glide);
    applied++;
  }
  if (m->probability != ACTION_LFO_ORIG_U8) {
    uint8_t prob = lfo_modify_resolve_u8_table(m->probability, prob_values,
      sizeof(prob_values) / sizeof(prob_values[0]));
    if (rtg) rtg_set_probability(prob);
    else sample_hold_set_probability(prob);
    applied++;
  }
  return applied;
}

action_handle_result_t action_handlers_modulation_dispatch(
    const action_t* action, uint8_t trigger_value, bool is_press, uint8_t channel) {
  (void)trigger_value;

  switch (action->type) {
    case ACTION_STREAM: {
      stream_target_t target = (stream_target_t)action->params.stream.target;
      if (target >= STREAM_TARGET_COUNT) target = STREAM_TARGET_DEFAULT;
      switch (action->variant) {
        case VARIANT_START:
          if (!is_press) return ACTION_HANDLED;
          stream_set_active(target, true);
          ESP_LOGI(TAG, "Stream Start: %s", stream_target_display_name(target));
          return ACTION_HANDLED;
        case VARIANT_STOP:
          if (!is_press) return ACTION_HANDLED;
          stream_set_active(target, false);
          ESP_LOGI(TAG, "Stream Stop: %s", stream_target_display_name(target));
          return ACTION_HANDLED;
        case VARIANT_TOGGLE:
          if (!is_press) return ACTION_HANDLED;
          stream_toggle(target);
          ESP_LOGI(TAG, "Stream Toggle: %s", stream_target_display_name(target));
          return ACTION_HANDLED;
        case VARIANT_HOLD:
          if (action->params.stream.hold_mode == STREAM_HOLD_CUT) {
            action_t* live = (action_t*)action;
            if (is_press) {
              live->params.stream.captured = stream_snapshot_active(target);
              stream_set_active(target, false);
            } else {
              stream_restore_active(target, live->params.stream.captured);
            }
            ESP_LOGD(TAG, "Stream Hold Cut: %s %s",
              stream_target_display_name(target), is_press ? "press" : "release");
            return ACTION_HANDLED;
          }
          stream_set_active(target, is_press);
          ESP_LOGD(TAG, "Stream Hold: %s %s",
            stream_target_display_name(target), is_press ? "press" : "release");
          return ACTION_HANDLED;
        default:
          ESP_LOGW(TAG, "Unknown Stream variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }
    }

    case ACTION_LFO: {
      uint8_t slot = action->params.lfo.slot;
      if (!is_press) return ACTION_HANDLED;
      const action_t* a = action;
      int applied = 0;
      for (int side = 0; side < 2; side++) {
        uint8_t lfo_index = (uint8_t)side;
        uint8_t slot_bit = (side == 0) ? 1 : 2;
        if (!(slot == slot_bit || slot == 3)) continue;

        if (a->params.lfo.waveform != ACTION_LFO_ORIG_U8) {
          static const uint8_t waveforms[] = {
            LFO_WAVEFORM_SINE, LFO_WAVEFORM_TRIANGLE, LFO_WAVEFORM_SQUARE,
            LFO_WAVEFORM_SAW_UP, LFO_WAVEFORM_SAW_DOWN, LFO_WAVEFORM_SAMPLE_HOLD,
            LFO_WAVEFORM_BIN, LFO_WAVEFORM_GLIDER, LFO_WAVEFORM_STRAY,
          };
          uint8_t wf = lfo_modify_resolve_u8_table(a->params.lfo.waveform,
            waveforms, sizeof(waveforms) / sizeof(waveforms[0]));
          lfo_set_waveform(lfo_index, (lfo_waveform_t)wf);
          applied++;
        }
        if (a->params.lfo.rate_mode != ACTION_LFO_ORIG_U8) {
          static const uint8_t rate_modes[] = {
            LFO_RATE_MODE_FREE,
            LFO_RATE_MODE_TEMPO,
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

    case ACTION_CUT:
      switch (action->variant) {
        case VARIANT_TOGGLE:
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

        case VARIANT_HOLD:
          {
            uint8_t cut_mode = action->params.cut.cut_mode;
            bool cut_active = is_press;
            if (cut_mode == 0 || cut_mode == 2)
              midi_out_set_cut_local(cut_active);
            if (cut_mode == 1 || cut_mode == 2)
              midi_out_set_cut_passthrough(cut_active);
            ESP_LOGI(TAG, "Cut Hold: mode %d, cut %s",
              cut_mode, cut_active ? "active" : "released");
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unknown Cut variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

    case ACTION_RTG:
      switch (action->variant) {
        case VARIANT_STEP:
          if (is_press) {
            rtg_step();
            ESP_LOGD(TAG, "RTG Step");
          }
          return ACTION_HANDLED;

        case VARIANT_MODIFY:
          if (is_press) {
            int n = apply_engine_modify(&action->params.rtg_modify, true);
            ESP_LOGI(TAG, "RTG Modify: %d override(s) applied", n);
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unknown RTG variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

    case ACTION_SAMPLE_HOLD:
      switch (action->variant) {
        case VARIANT_STEP:
          if (is_press) {
            sample_hold_step();
            ESP_LOGD(TAG, "S+H Step");
          }
          return ACTION_HANDLED;

        case VARIANT_MODIFY:
          if (is_press) {
            int n = apply_engine_modify(&action->params.sh_modify, false);
            ESP_LOGI(TAG, "S+H Modify: %d override(s) applied", n);
          }
          return ACTION_HANDLED;

        default:
          ESP_LOGW(TAG, "Unknown S+H variant %d", (int)action->variant);
          return ACTION_HANDLED;
      }

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

    default:
      return ACTION_NOT_HANDLED;
  }
}
