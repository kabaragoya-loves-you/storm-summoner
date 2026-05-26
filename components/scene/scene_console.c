#include "scene_console.h"
#include "scene.h"
#include "scene_name_gen.h"
#include "device_config.h"
#include "assets_manager.h"
#include "touchwheel_mode_mapping.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "scene_console";

// Helper to format CC numbers for display (handles multi-CC)
static void format_cc_list(const continuous_mapping_t* mapping, char* buf, size_t buf_size) {
  if (mapping->num_cc_numbers > 0) {
    int pos = 0;
    for (int i = 0; i < mapping->num_cc_numbers && i < MAX_MULTI_CC; i++) {
      if (i == 0) {
        pos += snprintf(buf + pos, buf_size - pos, "CC%d", mapping->cc_numbers[i]);
      } else {
        pos += snprintf(buf + pos, buf_size - pos, "+%d", mapping->cc_numbers[i]);
      }
    }
  } else {
    snprintf(buf, buf_size, "CC%d", mapping->cc_number);
  }
}

// Parse a 1-based scene number from console input and convert to the
// internal 0-based target.number. The console interface is documented as
// "<1-128>" throughout, so the +/- 1 conversion belongs here at the parser
// boundary rather than at execution time.
// Returns true on success; on failure logs an error and returns false so
// the caller can `return 1;`.
static bool parse_scene_number_1based(const char* str, const char* usage,
                                      uint8_t* out) {
  int n = str ? atoi(str) : 0;
  if (n < 1 || n > 128) {
    ESP_LOGE(TAG, "Scene number must be 1-128 (got %d). %s", n, usage);
    return false;
  }
  *out = (uint8_t)(n - 1);
  return true;
}

// Parse a piano pedal argument: either one of the five bare verbs
// (damper, sostenuto, soft, legato, hold2) or a numeric CC restricted to
// the five-CC whitelist. Returns 0 on failure (caller should treat that
// as an error and bail). On success returns the CC number.
static uint8_t parse_piano_pedal_arg(const char* arg) {
  if (!arg) return 0;
  if (strcmp(arg, "damper")    == 0) return 64;
  if (strcmp(arg, "sostenuto") == 0) return 66;
  if (strcmp(arg, "soft")      == 0) return 67;
  if (strcmp(arg, "legato")    == 0) return 68;
  if (strcmp(arg, "hold2")     == 0) return 69;
  int n = atoi(arg);
  switch (n) {
    case 64: case 66: case 67: case 68: case 69: return (uint8_t)n;
    default: return 0;
  }
}

// Shared touchwheel parser. Accepts:
//   variant = "hold" : params = <press_mode> <release_mode | original>
//   variant = "cycle": params = <mode1> <mode2> ... (up to 8 modes)
// Returns true on success and fills `action` (type + variant + tw_mode union).
// Logs the appropriate usage line and returns false on failure. `prefix`
// describes the trigger context for the usage line, e.g. "pad <num>" or
// "button <name>", so the user knows which command they were typing.
static bool parse_touchwheel_action(const char* variant,
                                    action_t* action,
                                    const char* const* params,
                                    int count,
                                    const char* prefix) {
  if (!variant || !action || !prefix) return false;
  action->type = ACTION_TOUCHWHEEL;
  if (strcmp(variant, "hold") == 0) {
    if (count < 2) {
      ESP_LOGE(TAG, "Usage: %s touchwheel hold <press_mode> <release_mode|original>", prefix);
      return false;
    }
    action->variant = VARIANT_HOLD;
    action->params.tw_mode.mode = (uint8_t)atoi(params[0]);
    action->params.tw_mode.release_to_original = 0;
    action->params.tw_mode.captured_mode = 0;
    if (params[1] && strcmp(params[1], "original") == 0) {
      action->params.tw_mode.release_to_original = 1;
      action->params.tw_mode.mode2 = 0;
    } else {
      action->params.tw_mode.mode2 = (uint8_t)atoi(params[1]);
    }
    return true;
  }
  if (strcmp(variant, "cycle") == 0) {
    if (count < 2) {
      ESP_LOGE(TAG, "Usage: %s touchwheel cycle <m1> <m2>...<m8>", prefix);
      return false;
    }
    action->variant = VARIANT_CYCLE;
    action->params.tw_mode.num_modes = 0;
    for (int i = 0; i < count && action->params.tw_mode.num_modes < 8; i++) {
      action->params.tw_mode.modes[action->params.tw_mode.num_modes++] =
        (uint8_t)atoi(params[i]);
    }
    action->params.tw_mode.current_index = 0;
    return true;
  }
  ESP_LOGE(TAG, "Usage: %s touchwheel <hold|cycle> ...", prefix);
  return false;
}

// Helper to format CC value with optional discrete name
static void format_cc_value_with_discrete(const device_def_t* device, uint8_t cc_num,
  uint16_t value, char* buf, size_t buf_size) {
  if (device) {
    const char* discrete_name = assets_get_discrete_name(device, cc_num, value);
    if (discrete_name) {
      snprintf(buf, buf_size, "%d (%s)", value, discrete_name);
      return;
    }
  }
  snprintf(buf, buf_size, "%d", value);
}

// Helper to format action details for display (device-aware version)
static void format_action_details_with_device(const action_t* action, const device_def_t* device,
  char* buf, size_t buf_size) {
  switch (action->type) {
    case ACTION_CONTROL: {
      uint8_t num_ccs = action->params.control.num_ccs;
      if (num_ccs == 0) num_ccs = 1;  // Backward compat
      uint8_t cc = action->params.control.cc_numbers[0];
      const char* cc_name = device ? assets_get_cc_name(device, cc) : NULL;

      switch (action->variant) {
        case VARIANT_SET: {
          if (num_ccs == 1) {
            char val_buf[32];
            format_cc_value_with_discrete(device, cc, action->params.control.values[0], val_buf, sizeof(val_buf));
            if (cc_name && strcmp(cc_name, "Undefined") != 0) {
              snprintf(buf, buf_size, "CC%d %s - %s", cc, cc_name, val_buf);
            } else {
              snprintf(buf, buf_size, "CC%d=%s", cc, val_buf);
            }
          } else {
            int pos = snprintf(buf, buf_size, "MultiCC:");
            for (int i = 0; i < num_ccs && i < 4 && pos < (int)buf_size - 10; i++) {
              pos += snprintf(buf + pos, buf_size - pos, " %d=%d",
                action->params.control.cc_numbers[i], action->params.control.values[i]);
            }
          }
          break;
        }
        case VARIANT_HOLD: {
          if (num_ccs == 1) {
            if (cc_name && strcmp(cc_name, "Undefined") != 0) {
              snprintf(buf, buf_size, "CC%d %s hold:%d/%d", cc, cc_name,
                action->params.control.values[0], action->params.control.values2[0]);
            } else {
              snprintf(buf, buf_size, "CC%d hold:%d/%d", cc,
                action->params.control.values[0], action->params.control.values2[0]);
            }
          } else {
            int pos = snprintf(buf, buf_size, "MultiCC hold:");
            for (int i = 0; i < num_ccs && i < 4 && pos < (int)buf_size - 15; i++) {
              pos += snprintf(buf + pos, buf_size - pos, " %d:%d/%d",
                action->params.control.cc_numbers[i], action->params.control.values[i], action->params.control.values2[i]);
            }
          }
          break;
        }
        case VARIANT_CYCLE: {
          int pos;
          if (num_ccs == 1) {
            if (cc_name && strcmp(cc_name, "Undefined") != 0) {
              pos = snprintf(buf, buf_size, "CC%d %s cycle:", cc, cc_name);
            } else {
              pos = snprintf(buf, buf_size, "CC%d cycle:", cc);
            }
            for (int i = 0; i < action->params.control.num_cycle_steps && pos < (int)buf_size - 4; i++) {
              pos += snprintf(buf + pos, buf_size - pos, "%s%d", i > 0 ? "," : "",
                action->params.control.cycle_values[0][i]);
            }
          } else {
            pos = snprintf(buf, buf_size, "MultiCC cycle (%d CCs, %d steps)",
              num_ccs, action->params.control.num_cycle_steps);
          }
          break;
        }
        default:
          snprintf(buf, buf_size, "Control(unknown variant %d)", (int)action->variant);
          break;
      }
      break;
    }
    case ACTION_NOTE: {
      uint8_t voices = action->params.note.voices;
      if (voices < 1) voices = 1;
      if (voices > 4) voices = 4;

      const char* vel_name = NULL;
      if (action->params.note.velocity == ACTION_NOTE_VEL_RANDOM) vel_name = "Random";
      else if (action->params.note.velocity == 127) vel_name = "Forte";
      else if (action->params.note.velocity == 100) vel_name = "Strong";
      else if (action->params.note.velocity == 80) vel_name = "Medium";
      else if (action->params.note.velocity == 60) vel_name = "Soft";
      else if (action->params.note.velocity == 40) vel_name = "Piano";

      int pos;
      if (action->params.note.note == ACTION_NOTE_RANDOM) {
        uint8_t lo = action->params.note.random_floor;
        uint8_t hi = action->params.note.random_ceiling;
        if (lo < 36) lo = 36;
        if (hi > 96) hi = 96;
        if (lo == 36 && hi == 96) {
          pos = snprintf(buf, buf_size, "Note Random");
        } else {
          pos = snprintf(buf, buf_size, "Note Random %u-%u", (unsigned)lo, (unsigned)hi);
        }
      } else {
        pos = snprintf(buf, buf_size, "Note %d", action->params.note.note);
      }
      if (vel_name) pos += snprintf(buf + pos, buf_size - pos, " %s", vel_name);
      else pos += snprintf(buf + pos, buf_size - pos, " vel=%d", action->params.note.velocity);
      if (voices > 1 && pos < (int)buf_size - 8)
        pos += snprintf(buf + pos, buf_size - pos, " x%u", (unsigned)voices);
      if (action->params.note.bass && pos < (int)buf_size - 8)
        pos += snprintf(buf + pos, buf_size - pos, " Bass");
      if (action->params.note.aftertouch && pos < (int)buf_size - 8)
        snprintf(buf + pos, buf_size - pos, " AT");
      break;
    }
    case ACTION_PIANO_PEDAL: {
      const char* pedal_name;
      switch (action->params.piano_pedal.cc_number) {
        case 64: pedal_name = "Damper";    break;
        case 66: pedal_name = "Sostenuto"; break;
        case 67: pedal_name = "Soft";      break;
        case 68: pedal_name = "Legato";    break;
        case 69: pedal_name = "Hold 2";    break;
        default: pedal_name = "Damper";    break;
      }
      snprintf(buf, buf_size, "Piano Pedal: %s (CC%u)", pedal_name,
        (unsigned)action->params.piano_pedal.cc_number);
      break;
    }
    case ACTION_RANDOMIZE: {
      int pos = snprintf(buf, buf_size, "Randomize CC");
      for (int i = 0; i < action->params.randomize.num_ccs && pos < (int)buf_size - 4; i++) {
        pos += snprintf(buf + pos, buf_size - pos, "%s%d", i > 0 ? "," : "",
          action->params.randomize.cc_numbers[i]);
      }
      break;
    }
    case ACTION_PRESET:
      // Variant decides the shape; SET uses the historical "PC <n>" form so
      // existing tooling/scripts parsing console output keep working.
      switch (action->variant) {
        case VARIANT_SET:
          snprintf(buf, buf_size, "PC %u", (unsigned)action->params.preset.program);
          break;
        case VARIANT_HOLD:
          if (action->params.preset.release_to_original) {
            snprintf(buf, buf_size, "PCH %u/Orig",
              (unsigned)action->params.preset.press_preset);
          } else {
            snprintf(buf, buf_size, "PCH %u/%u",
              (unsigned)action->params.preset.press_preset,
              (unsigned)action->params.preset.release_preset);
          }
          break;
        case VARIANT_CYCLE:
          snprintf(buf, buf_size, "PCY %u",
            (unsigned)action->params.preset.num_presets);
          break;
        case VARIANT_INCREMENT:
        case VARIANT_DECREMENT:
        default:
          action_get_display_name(action, buf, buf_size);
          break;
      }
      break;
    case ACTION_SCENE:
      // Only VARIANT_SET targets a numbered scene; surface 1-based to match
      // the menu summary and the documented `<1-128>` console usage.
      // INCREMENT/DECREMENT carry no number -- show the variant-aware
      // display name ("Scene +1" / "Scene -1") instead of the bare family.
      if (action->variant == VARIANT_SET) {
        snprintf(buf, buf_size, "Scene %u",
          (unsigned)action->params.target.number + 1);
      } else {
        action_get_display_name(action, buf, buf_size);
      }
      break;
    case ACTION_TEMPO:
      if (action->variant == VARIANT_SET) {
        if (action->params.tempo.bpm == ACTION_TEMPO_BPM_ORIGINAL)
          snprintf(buf, buf_size, "Tempo Original");
        else if (action->params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM)
          snprintf(buf, buf_size, "Tempo Random");
        else
          snprintf(buf, buf_size, "Tempo %d BPM", action->params.tempo.bpm);
      } else {
        snprintf(buf, buf_size, "%s", action_type_to_string(action->type));
      }
      break;
    case ACTION_TRANSPORT:
      // No per-variant params -- the variant-aware display name ("Play" /
      // "Stop" / "Pause" / "Record") matches what the console showed when
      // each was a separate top-level action type.
      action_get_display_name(action, buf, buf_size);
      break;
    case ACTION_TOUCHWHEEL:
      switch (action->variant) {
        case VARIANT_HOLD: {
          const char* press_name = touchwheel_get_mode_name(action->params.tw_mode.mode);
          if (action->params.tw_mode.release_to_original) {
            snprintf(buf, buf_size, "Touchwheel Hold %s/Orig", press_name);
          } else {
            const char* release_name = touchwheel_get_mode_name(action->params.tw_mode.mode2);
            snprintf(buf, buf_size, "Touchwheel Hold %s/%s", press_name, release_name);
          }
          break;
        }
        case VARIANT_CYCLE: {
          int pos = snprintf(buf, buf_size, "TW cycle:");
          for (int i = 0; i < action->params.tw_mode.num_modes && pos < (int)buf_size - 4; i++) {
            pos += snprintf(buf + pos, buf_size - pos, "%s%s", i > 0 ? "," : " ",
              touchwheel_get_mode_name(action->params.tw_mode.modes[i]));
          }
          break;
        }
        default:
          snprintf(buf, buf_size, "%s", action_type_to_string(action->type));
          break;
      }
      break;
    case ACTION_LFO: {
      // Console-side variant-aware label. No console parser verbs for the
      // LFO family yet (the inventory carried none for the old singletons
      // either); these print-only formatters keep `scene show` and the
      // pad-listing readouts intelligible while we wait to add LFO console
      // commands as a follow-up task. The leading display-name ("LFO
      // Start"/"LFO Modify"/etc.) already encodes the variant, so we
      // only tag on the bits the user actually configured.
      const char* slot_name =
        action->params.lfo.slot == 1 ? "LFO1" :
        action->params.lfo.slot == 2 ? "LFO2" :
        action->params.lfo.slot == 3 ? "Both" : "LFO?";
      if (action->variant == VARIANT_MODIFY) {
        // List override tags compactly. Same naming as action_summary so
        // the user sees consistent terminology everywhere.
        int pos = snprintf(buf, buf_size, "LFO Modify %s:", slot_name);
        int first = 1;
        #define LFO_DBG_TAG(_tag) do { \
          if (pos < (int)buf_size - 16) { \
            pos += snprintf(buf + pos, buf_size - pos, "%s%s", \
              first ? " " : ",", (_tag)); \
            first = 0; \
          } \
        } while (0)
        if (action->params.lfo.waveform        != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Wave");
        if (action->params.lfo.rate_mode       != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("RateMode");
        if (action->params.lfo.rate_hz_x100    != ACTION_LFO_ORIG_U16)   LFO_DBG_TAG("Rate");
        if (action->params.lfo.division        != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Div");
        if (action->params.lfo.polarity        != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Pol");
        if (action->params.lfo.floor           != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Floor");
        if (action->params.lfo.ceiling         != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Ceil");
        if (action->params.lfo.resolution_mode != ACTION_LFO_ORIG_U8)    LFO_DBG_TAG("Res");
        if (action->params.lfo.manual_steps    != ACTION_LFO_ORIG_STEPS) LFO_DBG_TAG("Steps");
        if (first) snprintf(buf + pos, buf_size - pos, " no overrides");
        #undef LFO_DBG_TAG
      } else {
        snprintf(buf, buf_size, "%s %s",
          action_variant_to_string(action->variant) /* Start/Stop/Toggle */, slot_name);
        // Fall back if variant string is empty (shouldn't happen for the
        // consolidated family; defensive only).
        if (buf[0] == ' ') {
          snprintf(buf, buf_size, "LFO ? %s", slot_name);
        }
      }
      break;
    }
    default:
      // For actions without parameters, just use the action name
      snprintf(buf, buf_size, "%s", action_type_to_string(action->type));
      break;
  }
}

// Helper to format action details for display (legacy wrapper)
static void format_action_details(const action_t* action, char* buf, size_t buf_size) {
  format_action_details_with_device(action, NULL, buf, buf_size);
}

// Track registered command names for cleanup
static const char* registered_commands[] = {
  "info", "next", "prev", "goto", "name", "device",
  "confirm", "cancel", "channel", "pad", "button", "bump", "expr_switch", "actions", "pc",
  "expr_cc", "expr_curve", "expr_polarity", "expr_enable", "expr_output", "expr_base_note", "expr_note_range", "expr_velocity", "expr_mode",
  "cv_cc", "cv_curve", "cv_polarity", "cv_enable", "cv_output", "cv_base_note", "cv_note_range", "cv_velocity", "cv_input_mode",
  "cv_velocity_mode", "cv_note_velocity",
  "bpm", "clock_source", "beat_divider", "time_sig", "use_transport", "send_clock",
  "proximity_cc", "proximity_curve", "proximity_polarity", "proximity_enable", "proximity_output", "proximity_base_note", "proximity_note_range", "proximity_velocity",
  "proximity_velocity_mode",
  "als_cc", "als_curve", "als_polarity", "als_enable", "als_output", "als_base_note", "als_note_range", "als_velocity",
  "als_velocity_mode",
  "lfo1_cc", "lfo1_curve", "lfo1_polarity", "lfo1_enable", "lfo1_output", "lfo1_base_note", "lfo1_note_range", "lfo1_velocity", "lfo1_velocity_mode",
  "lfo1_repeat", "lfo1_trigger",
  "lfo2_cc", "lfo2_curve", "lfo2_polarity", "lfo2_enable", "lfo2_output", "lfo2_base_note", "lfo2_note_range", "lfo2_velocity", "lfo2_velocity_mode",
  "lfo2_repeat", "lfo2_trigger",
  "touchwheel_mode", "touchwheel_style", "touchwheel_enable", "touchwheel_output", "touchwheel_cc", "touchwheel_note",
  "on_load", "expression_velocity_mode", "active"
};
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Helper to print scene info
static void cmd_scene_info(void) {
  uint8_t index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  
  if (!scene) {
    ESP_LOGE(TAG, "Scene manager not initialized!");
    return;
  }
  
  // Get device for enhanced info display
  const device_def_t* device = (const device_def_t*)scene_get_device(index);
  
  ESP_LOGI(TAG, "====== SCENE INFO ======");
  ESP_LOGI(TAG, "Current scene: %d - %s (%s)", index + 1, scene->name,
    scene_is_active(index) ? "active" : "inactive");
  
  // Display device info
  const char* effective_slug = scene_get_effective_device_slug(index);
  if (scene->device_id[0] != '\0') {
    ESP_LOGI(TAG, "Device: %s (scene-specific)", scene->device_id);
  } else if (effective_slug && effective_slug[0] != '\0') {
    ESP_LOGI(TAG, "Device: %s (global)", effective_slug);
  } else {
    ESP_LOGI(TAG, "Device: (none configured)");
  }
  if (device) {
    ESP_LOGI(TAG, "  Name: %s", device->name[0] ? device->name : "(unknown)");
    ESP_LOGI(TAG, "  Controls: %u CCs defined", (unsigned)device->control_count);
  }
  
  ESP_LOGI(TAG, "Program number: %d (send PC on load: %s)", scene->program_number, 
           scene->send_pc_on_load ? "yes" : "no");
  ESP_LOGI(TAG, "On-load actions: %d", scene->num_on_load_actions);
  if (scene->num_on_load_actions > 0) {
    for (int i = 0; i < scene->num_on_load_actions && i < MAX_ON_LOAD_ACTIONS; i++) {
      ESP_LOGI(TAG, "  [%d] %s", i, action_type_to_string(scene->on_load[i].type));
    }
  }
  const char* tw_mode_str;
  switch (scene->touchwheel_mode) {
    case TOUCHWHEEL_MODE_PADS: tw_mode_str = "pads"; break;
    case TOUCHWHEEL_MODE_PROGRAM_CHANGE: tw_mode_str = "program_change"; break;
    case TOUCHWHEEL_MODE_SET_TEMPO: tw_mode_str = "set_tempo"; break;
    case TOUCHWHEEL_MODE_PITCH_BEND: tw_mode_str = "pitch_bend"; break;
    case TOUCHWHEEL_MODE_AFTERTOUCH: tw_mode_str = "aftertouch"; break;
    case TOUCHWHEEL_MODE_DOUBLE_CC: tw_mode_str = "double_cc"; break;
    default: tw_mode_str = "continuous"; break;
  }
  if (scene->touchwheel_mode != TOUCHWHEEL_MODE_PADS && 
      scene->touchwheel_mode != TOUCHWHEEL_MODE_PROGRAM_CHANGE) {
    const char* tw_style_str = (scene->touchwheel_style == TOUCHWHEEL_STYLE_BIPOLAR) ? "bipolar" :
                               (scene->touchwheel_style == TOUCHWHEEL_STYLE_ENDLESS) ? "endless" : "odometer";
    ESP_LOGI(TAG, "Touchwheel mode: %s (%s)", tw_mode_str, tw_style_str);
  } else {
    ESP_LOGI(TAG, "Touchwheel mode: %s", tw_mode_str);
  }
  
  if (scene_has_pending_change()) {
    ESP_LOGI(TAG, "PENDING CHANGE to scene %d", scene_get_pending_index() + 1);
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Button assignments:");
  char action_buf[64];
  if (scene->button_left.type != ACTION_NONE) {
    format_action_details(&scene->button_left, action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Left: %s", action_buf);
  } else {
    ESP_LOGI(TAG, "  Left: no action");
  }
  
  if (scene->button_right.type != ACTION_NONE) {
    format_action_details(&scene->button_right, action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Right: %s", action_buf);
  } else {
    ESP_LOGI(TAG, "  Right: no action");
  }
  
  if (scene->button_both.type != ACTION_NONE) {
    format_action_details(&scene->button_both, action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Both: %s", action_buf);
  } else {
    ESP_LOGI(TAG, "  Both: no action");
  }
  
  if (scene->bump.type != ACTION_NONE) {
    format_action_details(&scene->bump, action_buf, sizeof(action_buf));
    ESP_LOGI(TAG, "  Bump: %s", action_buf);
  } else {
    ESP_LOGI(TAG, "  Bump: no action");
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Tempo settings:");
  ESP_LOGI(TAG, "  BPM: %d", scene->bpm);
  ESP_LOGI(TAG, "  Clock source: %s",
           scene->clock_source == CLOCK_SOURCE_INTERNAL ? "Internal" :
           scene->clock_source == CLOCK_SOURCE_MIDI ? "MIDI" : "Sync");
  ESP_LOGI(TAG, "  Beat divider: %s",
           scene->beat_divider == DIVIDER_QUARTER ? "Quarter" :
           scene->beat_divider == DIVIDER_EIGHTH ? "Eighth" : "Sixteenth");
  ESP_LOGI(TAG, "  Time signature: %d/%d",
           scene->time_signature.numerator, scene->time_signature.denominator);
  ESP_LOGI(TAG, "  Send clock: %s", scene->send_clock ? "Yes" : "No");
  
  // Only show expression jack mode section for action-based modes (sustain/sostenuto/gate/switch)
  if (scene->expression_mode != EXPRESSION_MODE_PEDAL) {
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Expression jack mode: %s", 
             scene->expression_mode == EXPRESSION_MODE_SUSTAIN ? "sustain" :
             scene->expression_mode == EXPRESSION_MODE_SOSTENUTO ? "sostenuto" :
             scene->expression_mode == EXPRESSION_MODE_SWITCH ? "switch" : "gate");
    
    if (scene->expression_mode == EXPRESSION_MODE_SUSTAIN) {
      if (scene->sustain.type != ACTION_NONE) {
        ESP_LOGI(TAG, "  Sustain: %s", action_type_to_string(scene->sustain.type));
      } else {
        ESP_LOGI(TAG, "  Sustain: no action");
      }
    } else if (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO) {
      if (scene->sostenuto.type != ACTION_NONE) {
        ESP_LOGI(TAG, "  Sostenuto: %s", action_type_to_string(scene->sostenuto.type));
      } else {
        ESP_LOGI(TAG, "  Sostenuto: no action");
      }
    } else if (scene->expression_mode == EXPRESSION_MODE_SWITCH) {
      if (scene->expr_switch.type != ACTION_NONE) {
        ESP_LOGI(TAG, "  Switch: %s", action_type_to_string(scene->expr_switch.type));
      } else {
        ESP_LOGI(TAG, "  Switch: no action");
      }
    }
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "CV Input Mode: %s",
           scene->cv_input_mode == INPUT_MODE_NONE ? "<None>" :
           scene->cv_input_mode == INPUT_MODE_CV ? "CV" :
           scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC ? "Clock Sync" :
           scene->cv_input_mode == INPUT_MODE_AUDIO ? "Audio" : "Note");
  
  // Display CV velocity settings when in NOTE input mode
  if (scene->cv_input_mode == INPUT_MODE_NOTE) {
    const char* cv_vel_mode_str = scene->cv_velocity_mode == VELOCITY_MODE_FIXED ? "Fixed" :
                                  scene->cv_velocity_mode == VELOCITY_MODE_GATE_VOLTAGE ? "Gate Voltage" : "Touchwheel";
    ESP_LOGI(TAG, "  CV velocity mode: %s", cv_vel_mode_str);
    if (scene->cv_velocity_mode == VELOCITY_MODE_FIXED) {
      ESP_LOGI(TAG, "  CV velocity: %d", scene->cv_velocity);
    }
  }
  
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Continuous inputs:");
  
  // Expression (only shown here when in pedal mode)
  if (scene->expression_mode == EXPRESSION_MODE_PEDAL) {
    if (scene->expression.enabled) {
      if (scene->expression.output_type == OUTPUT_TYPE_NOTE) {
        ESP_LOGI(TAG, "  Expression: NOTE (base=%d, range=%d, vel=%d), %s curve", 
                 scene->expression.base_note, scene->expression.note_range, scene->expression.velocity,
                 curve_type_to_string(scene->expression.curve.type));
      } else {
        char cc_buf[32];
        format_cc_list(&scene->expression, cc_buf, sizeof(cc_buf));
        ESP_LOGI(TAG, "  Expression: %s, %s curve, %s", 
                 cc_buf,
                 curve_type_to_string(scene->expression.curve.type),
                 scene->expression.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
                 (scene->expression.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
      }
    } else {
      ESP_LOGI(TAG, "  Expression: disabled");
    }
  }
  
  // Touchwheel (only shown here when in continuous mode)
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    if (scene->touchwheel.enabled) {
      if (scene->touchwheel.output_type == OUTPUT_TYPE_NOTE) {
        ESP_LOGI(TAG, "  Touchwheel: NOTE (base=%d, range=%d, vel=%d)", 
                 scene->touchwheel.base_note, scene->touchwheel.note_range, scene->touchwheel.velocity);
      } else {
        char cc_buf[32];
        format_cc_list(&scene->touchwheel, cc_buf, sizeof(cc_buf));
        ESP_LOGI(TAG, "  Touchwheel: %s", cc_buf);
      }
    } else {
      ESP_LOGI(TAG, "  Touchwheel: disabled");
    }
  }
  
  // CV
  if (scene->cv.enabled) {
    if (scene->cv.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  CV: NOTE (base=%d, range=%d, vel=%d), %s curve", 
               scene->cv.base_note, scene->cv.note_range, scene->cv.velocity,
               curve_type_to_string(scene->cv.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->cv, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  CV: %s, %s curve, %s", 
               cc_buf,
               curve_type_to_string(scene->cv.curve.type),
               scene->cv.polarity == POLARITY_UNIPOLAR ? "unipolar" : 
               (scene->cv.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
    }
  } else {
    ESP_LOGI(TAG, "  CV: disabled");
  }
  
  // Proximity
  if (scene->proximity.enabled) {
    if (scene->proximity.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  Proximity: NOTE (base=%d, range=%d, vel=%d), %s curve%s", 
               scene->proximity.base_note, scene->proximity.note_range, scene->proximity.velocity,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    } else {
      char cc_buf[32];
      format_cc_list(&scene->proximity, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  Proximity: %s, %s curve, bipolar%s", 
               cc_buf,
               curve_type_to_string(scene->proximity.curve.type),
               scene->proximity.use_idle_value ? " (idle timeout)" : "");
    }
  } else {
    ESP_LOGI(TAG, "  Proximity: disabled");
  }
  
  // ALS
  if (scene->als.enabled) {
    if (scene->als.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  ALS: NOTE (base=%d, range=%d, vel=%d), %s curve", 
               scene->als.base_note, scene->als.note_range, scene->als.velocity,
               curve_type_to_string(scene->als.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->als, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  ALS: %s, %s curve", 
               cc_buf,
               curve_type_to_string(scene->als.curve.type));
    }
  } else {
    ESP_LOGI(TAG, "  ALS: disabled");
  }

  // LFO1
  if (scene->lfo1.enabled) {
    if (scene->lfo1.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  LFO1: NOTE (base=%d, range=%d, vel=%d), %s curve",
               scene->lfo1.base_note, scene->lfo1.note_range, scene->lfo1.velocity,
               curve_type_to_string(scene->lfo1.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->lfo1, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  LFO1: %s, %s curve, %s",
               cc_buf,
               curve_type_to_string(scene->lfo1.curve.type),
               scene->lfo1.polarity == POLARITY_UNIPOLAR ? "unipolar" :
               (scene->lfo1.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
    }
  } else {
    ESP_LOGI(TAG, "  LFO1: disabled");
  }

  // LFO2
  if (scene->lfo2.enabled) {
    if (scene->lfo2.output_type == OUTPUT_TYPE_NOTE) {
      ESP_LOGI(TAG, "  LFO2: NOTE (base=%d, range=%d, vel=%d), %s curve",
               scene->lfo2.base_note, scene->lfo2.note_range, scene->lfo2.velocity,
               curve_type_to_string(scene->lfo2.curve.type));
    } else {
      char cc_buf[32];
      format_cc_list(&scene->lfo2, cc_buf, sizeof(cc_buf));
      ESP_LOGI(TAG, "  LFO2: %s, %s curve, %s",
               cc_buf,
               curve_type_to_string(scene->lfo2.curve.type),
               scene->lfo2.polarity == POLARITY_UNIPOLAR ? "unipolar" :
               (scene->lfo2.polarity == POLARITY_BIPOLAR ? "bipolar" : "inverted"));
    }
  } else {
    ESP_LOGI(TAG, "  LFO2: disabled");
  }

  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Touchpad mappings:");
  
  // Skip pads 0-7 if touchwheel is active (not in pads mode)
  int start_pad = (scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS) ? 0 : TOUCHWHEEL_SIZE;
  if (start_pad > 0) {
    ESP_LOGI(TAG, "  Pads 0-7: (used by touchwheel)");
  }
  
  for (int i = start_pad; i < NUM_TOUCHPADS; i++) {
    touchpad_mapping_t* map = &scene->touchpads[i];
    if (map->enabled) {
      if (map->action.type != ACTION_NONE) {
        char pad_action_buf[96];  // Larger buffer for device names
        format_action_details_with_device(&map->action, device, pad_action_buf, sizeof(pad_action_buf));
        ESP_LOGI(TAG, "  Pad %2d: %s", i, pad_action_buf);
      } else {
        ESP_LOGI(TAG, "  Pad %2d: no action", i);
      }
    } else {
      ESP_LOGI(TAG, "  Pad %2d: disabled", i);
    }
  }
  ESP_LOGI(TAG, "========================");
}

// ESP Console command: info
static int cmd_console_scene_info(int argc, char **argv) {
  cmd_scene_info();
  return 0;
}

// Command: next
static int cmd_next(int argc, char **argv) {
  esp_err_t ret = scene_next();
  if (ret == ESP_OK) {
    if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
      ESP_LOGI(TAG, "Pending next scene (index %d)", scene_get_pending_index());
    } else {
      ESP_LOGI(TAG, "Switched to scene %d", scene_get_current_index() + 1);
    }
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
  }
  return 0;
}

// Command: prev
static int cmd_prev(int argc, char **argv) {
  esp_err_t ret = scene_previous();
  if (ret == ESP_OK) {
    if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
      ESP_LOGI(TAG, "Pending previous scene (index %d)", scene_get_pending_index());
    } else {
      ESP_LOGI(TAG, "Switched to scene %d", scene_get_current_index() + 1);
    }
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "Scene navigation disabled in single mode");
  }
  return 0;
}

// Command: goto
static struct {
  struct arg_int *scene_num;
  struct arg_end *end;
} goto_args;

static int cmd_goto(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &goto_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, goto_args.end, argv[0]);
    return 1;
  }
  
  int scene_num = goto_args.scene_num->ival[0];
  if (scene_num < 1 || scene_num > MAX_SCENE_INDEX) {
    ESP_LOGE(TAG, "Scene number must be 1-%d", MAX_SCENE_INDEX);
    return 1;
  }
  
  scene_set_current(scene_num - 1);
  if (scene_get_change_mode() == CHANGE_MODE_PENDING) {
    ESP_LOGI(TAG, "Pending change to scene %d", scene_num);
  } else {
    ESP_LOGI(TAG, "Switched to scene %d", scene_num);
  }
  return 0;
}

// Command: name [--generate | "name string"]
static struct {
  struct arg_lit *generate;
  struct arg_str *scene_name;
  struct arg_end *end;
} name_args;

static int cmd_name(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &name_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, name_args.end, argv[0]);
    return 1;
  }

  uint8_t scene_index = scene_get_current_index();

  // Handle --generate flag
  if (name_args.generate->count > 0) {
    if (!scene_name_gen_ready()) {
      ESP_LOGE(TAG, "Name generator not initialized");
      return 1;
    }
    char new_name[SCENE_NAME_MAX_LEN + 1];
    esp_err_t ret = ESP_ERR_INVALID_ARG;
    // Retry up to 10 times if name collision
    for (int attempt = 0; attempt < 10 && ret == ESP_ERR_INVALID_ARG; attempt++) {
      scene_name_generate(new_name, sizeof(new_name));
      ret = scene_set_name(scene_index, new_name);
    }
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Generated name: %s", new_name);
    } else {
      ESP_LOGE(TAG, "Failed to set generated name: %s", esp_err_to_name(ret));
      return 1;
    }
    return 0;
  }

  // Handle setting name directly
  if (name_args.scene_name->count > 0) {
    const char* name = name_args.scene_name->sval[0];
    esp_err_t ret = scene_set_name(scene_index, name);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Scene renamed to: %s", name);
    } else if (ret == ESP_ERR_INVALID_ARG) {
      ESP_LOGE(TAG, "Name '%s' is already in use", name);
      return 1;
    } else {
      ESP_LOGE(TAG, "Failed to rename scene: %s", esp_err_to_name(ret));
      return 1;
    }
    return 0;
  }

  // Show current name
  scene_t* scene = scene_get_current();
  if (scene && scene->name[0]) {
    ESP_LOGI(TAG, "Current name: %s", scene->name);
  } else {
    ESP_LOGI(TAG, "Scene has no name set");
  }
  ESP_LOGI(TAG, "Usage: name \"new name\" | name --generate");
  return 0;
}

// Command: device [slug | --clear]
// Show current device, set device, or clear (use global)
static struct {
  struct arg_str *slug;
  struct arg_lit *clear;
  struct arg_end *end;
} device_args;

static int cmd_device(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &device_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, device_args.end, argv[0]);
    return 1;
  }

  uint8_t index = scene_get_current_index();

  // Handle --clear flag
  if (device_args.clear->count > 0) {
    scene_clear_device_id(index);
    ESP_LOGI(TAG, "Device cleared for scene %d (now using global)", index + 1);
    return 0;
  }

  // Handle setting device
  if (device_args.slug->count > 0) {
    const char* slug = device_args.slug->sval[0];

    // Verify device exists before setting
    device_def_t* dev = assets_load_device(slug);
    if (!dev) {
      ESP_LOGE(TAG, "Device not found: %s", slug);
      ESP_LOGI(TAG, "Use 'device_list' in device context to see available devices");
      return 1;
    }
    assets_free_device(dev);

    scene_set_device_id(index, slug);
    ESP_LOGI(TAG, "Device set to: %s", slug);
    return 0;
  }

  // Show current device
  scene_t* scene = scene_get_current();
  if (!scene) {
    ESP_LOGE(TAG, "Scene not loaded");
    return 1;
  }

  const char* effective_slug = scene_get_effective_device_slug(index);
  if (scene->device_id[0] != '\0') {
    ESP_LOGI(TAG, "Scene device: %s (scene-specific)", scene->device_id);
  } else if (effective_slug && effective_slug[0] != '\0') {
    ESP_LOGI(TAG, "Scene device: %s (from global config)", effective_slug);
  } else {
    ESP_LOGI(TAG, "Scene device: (none configured)");
  }

  const device_def_t* device = (const device_def_t*)scene_get_device(index);
  if (device) {
    ESP_LOGI(TAG, "  Name: %s", device->name[0] ? device->name : "(unknown)");
    ESP_LOGI(TAG, "  Vendor: %s", device->vendor[0] ? device->vendor : "(unknown)");
    ESP_LOGI(TAG, "  Controls: %u", (unsigned)device->control_count);
  }

  return 0;
}

// Command: confirm
static int cmd_confirm(int argc, char **argv) {
  if (scene_has_pending_change()) {
    scene_confirm_change();
    ESP_LOGI(TAG, "Confirmed scene change to %d", scene_get_current_index() + 1);
  } else {
    ESP_LOGW(TAG, "No pending change to confirm");
  }
  return 0;
}

// Command: cancel
static int cmd_cancel(int argc, char **argv) {
  if (scene_has_pending_change()) {
    scene_cancel_pending();
    ESP_LOGI(TAG, "Cancelled pending change");
  } else {
    ESP_LOGW(TAG, "No pending change to cancel");
  }
  return 0;
}

// Note: channel command moved to midi context

//==============================================================================
// Multi-CC parsing helpers
// Supports syntax: <cc> <val> [/ <cc2> <val2> ...] for up to 4 CCs
//==============================================================================

typedef enum {
  MULTI_CC_MODE_CC,       // cc: 2 args per group (cc, value)
  MULTI_CC_MODE_CC_HOLD,  // cc_hold: 3 args per group (cc, press, release)
  MULTI_CC_MODE_CC_CYCLE  // cc_cycle: variable args per group (cc, v1, v2, ...)
} multi_cc_mode_t;

typedef struct {
  uint8_t num_ccs;            // Number of CCs (1-4)
  uint8_t cc_numbers[4];      // CC numbers
  uint8_t values[4];          // For CC: values; For HOLD: press values
  uint8_t values2[4];         // For HOLD: release values
  uint8_t num_cycle_steps;    // For CYCLE: number of steps (must match for all CCs)
  uint8_t cycle_values[4][8]; // For CYCLE: [cc_idx][step]
} multi_cc_result_t;

// Parse multi-CC from string arguments
// args: array of string arguments (starting from first CC number)
// arg_count: number of arguments
// mode: type of CC action being parsed
// result: output structure
// Returns 0 on success, -1 on error (logs error message)
static int parse_multi_cc(const char** args, int arg_count, multi_cc_mode_t mode,
  multi_cc_result_t* result) {
  memset(result, 0, sizeof(*result));
  
  if (arg_count < 2) {
    ESP_LOGE(TAG, "Not enough arguments for CC action");
    return -1;
  }
  
  int pos = 0;
  int first_group_cycle_count = 0;  // For cycle: track first group's value count
  
  while (pos < arg_count && result->num_ccs < 4) {
    // Parse CC number
    int cc_num = atoi(args[pos++]);
    if (cc_num < 0 || cc_num > 127) {
      ESP_LOGE(TAG, "CC number must be 0-127, got %d", cc_num);
      return -1;
    }
    result->cc_numbers[result->num_ccs] = cc_num;
    
    if (mode == MULTI_CC_MODE_CC) {
      // cc: need exactly 1 value
      if (pos >= arg_count || strcmp(args[pos], "/") == 0) {
        ESP_LOGE(TAG, "Missing value for CC%d", cc_num);
        return -1;
      }
      int val = atoi(args[pos++]);
      if (val < 0 || val > 127) {
        ESP_LOGE(TAG, "CC value must be 0-127, got %d", val);
        return -1;
      }
      result->values[result->num_ccs] = val;
    }
    else if (mode == MULTI_CC_MODE_CC_HOLD) {
      // cc_hold: need exactly 2 values (press, release)
      if (pos + 1 >= arg_count || strcmp(args[pos], "/") == 0 ||
        strcmp(args[pos + 1], "/") == 0) {
        ESP_LOGE(TAG, "Need press and release values for CC%d", cc_num);
        return -1;
      }
      int press = atoi(args[pos++]);
      int release = atoi(args[pos++]);
      if (press < 0 || press > 127 || release < 0 || release > 127) {
        ESP_LOGE(TAG, "CC values must be 0-127");
        return -1;
      }
      result->values[result->num_ccs] = press;
      result->values2[result->num_ccs] = release;
    }
    else if (mode == MULTI_CC_MODE_CC_CYCLE) {
      // cc_cycle: collect values until "/" or end
      int cycle_count = 0;
      while (pos < arg_count && strcmp(args[pos], "/") != 0 && cycle_count < 8) {
        int val = atoi(args[pos++]);
        if (val < 0 || val > 127) {
          ESP_LOGE(TAG, "Cycle value must be 0-127, got %d", val);
          return -1;
        }
        result->cycle_values[result->num_ccs][cycle_count++] = val;
      }
      
      if (cycle_count < 2) {
        ESP_LOGE(TAG, "Cycle needs at least 2 values for CC%d", cc_num);
        return -1;
      }
      
      // Enforce same number of values for all CCs
      if (result->num_ccs == 0) {
        first_group_cycle_count = cycle_count;
        result->num_cycle_steps = cycle_count;
      } else if (cycle_count != first_group_cycle_count) {
        ESP_LOGE(TAG, "All CCs must have same number of cycle values (%d vs %d)",
          first_group_cycle_count, cycle_count);
        return -1;
      }
    }
    
    result->num_ccs++;
    
    // Check for "/" separator
    if (pos < arg_count) {
      if (strcmp(args[pos], "/") == 0) {
        pos++;  // Skip the separator
        if (pos >= arg_count) {
          ESP_LOGE(TAG, "Trailing '/' without more CC definitions");
          return -1;
        }
      }
    }
  }
  
  if (result->num_ccs == 0) {
    ESP_LOGE(TAG, "No CCs parsed");
    return -1;
  }
  
  return 0;
}

// Command: pad <pad_num> <action_type> [params...]
// Examples:
//   pad 0 cc 74 127           - Send CC74=127
//   pad 0 cc 74 127 / 75 64   - Multi-CC: send CC74=127 and CC75=64
//   pad 1 note_on 60 100      - Send Note On C4 vel 100
//   pad 2 tap_tempo           - Tap tempo
//   pad 3 randomize 74        - Randomize CC74
static struct {
  struct arg_int *pad_num;
  struct arg_str *action_type;
  struct arg_str *params;  // String args to allow "/" separator for multi-CC
  struct arg_end *end;
} pad_args;

static int cmd_pad(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pad_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pad_args.end, argv[0]);
    return 1;
  }
  
  int pad = pad_args.pad_num->ival[0];
  const char* action_str = pad_args.action_type->sval[0];
  
  if (pad < 0 || pad >= NUM_TOUCHPADS) {
    ESP_LOGE(TAG, "Pad must be 0-%d", NUM_TOUCHPADS - 1);
    return 1;
  }
  
  // Get device for value validation (may be NULL)
  uint8_t scene_index = scene_get_current_index();
  const device_def_t* device = (const device_def_t*)scene_get_device(scene_index);
  
  action_t action = {0};
  
  // Parse action type and parameters
  if (strcmp(action_str, "cc") == 0) {
    // Multi-CC support: cc <cc> <val> [/ <cc2> <val2> ...]
    if (pad_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: pad <num> cc <cc> <val> [/ <cc2> <val2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(pad_args.params->sval, pad_args.params->count,
        MULTI_CC_MODE_CC, &mcc) != 0) {
      return 1;
    }
    
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_SET;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      int cc_num = mcc.cc_numbers[i];
      int value = mcc.values[i];
      // Device-aware validation
      if (device) {
        uint16_t clamped = assets_clamp_cc_value(device, cc_num, value);
        if (clamped != value) {
          ESP_LOGW(TAG, "CC%d value %d clamped to %d", cc_num, value, clamped);
          value = clamped;
        }
        if (assets_cc_has_discrete_values(device, cc_num)) {
          uint16_t snapped = assets_snap_to_discrete(device, cc_num, value);
          if (snapped != value) {
            const char* name = assets_get_discrete_name(device, cc_num, snapped);
            ESP_LOGI(TAG, "CC%d snapped to %d (%s)", cc_num, snapped, name ? name : "");
            value = snapped;
          }
        }
      }
      action.params.control.cc_numbers[i] = cc_num;
      action.params.control.values[i] = value;
    }
    ESP_LOGI(TAG, "CC action with %d CC(s)", mcc.num_ccs);
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    // Multi-CC support: cc_hold <cc> <press> <release> [/ <cc2> <press2> <release2> ...]
    if (pad_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_hold <cc> <press> <release> [/ <cc2> <p2> <r2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(pad_args.params->sval, pad_args.params->count,
        MULTI_CC_MODE_CC_HOLD, &mcc) != 0) {
      return 1;
    }
    
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_HOLD;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      int cc_num = mcc.cc_numbers[i];
      int press_val = mcc.values[i];
      int release_val = mcc.values2[i];
      // Device-aware validation
      if (device) {
        press_val = assets_clamp_cc_value(device, cc_num, press_val);
        release_val = assets_clamp_cc_value(device, cc_num, release_val);
        if (assets_cc_has_discrete_values(device, cc_num)) {
          press_val = assets_snap_to_discrete(device, cc_num, press_val);
          release_val = assets_snap_to_discrete(device, cc_num, release_val);
        }
      }
      action.params.control.cc_numbers[i] = cc_num;
      action.params.control.values[i] = press_val;
      action.params.control.values2[i] = release_val;
    }
    ESP_LOGI(TAG, "CC Hold action with %d CC(s)", mcc.num_ccs);
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (pad_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: pad <num> note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(pad_args.params->sval[0]);
    action.params.note.velocity = atoi(pad_args.params->sval[1]);
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> note_off <note>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(pad_args.params->sval[0]);
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> pc <program> (0-127 or 0-16383 with bank mode)");
      return 1;
    }
    action.type = ACTION_PRESET;
    action.variant = VARIANT_SET;
    action.params.preset.program = atoi(pad_args.params->sval[0]);
  }
  else if (strcmp(action_str, "randomize") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> randomize <cc_num> [cc2] [cc3] ...");
      return 1;
    }
    
    action.type = ACTION_RANDOMIZE;
    action.params.randomize.num_ccs = 0;
    
    // Collect all CC numbers from params array
    for (int i = 0; i < pad_args.params->count && action.params.randomize.num_ccs < 8; i++) {
      action.params.randomize.cc_numbers[action.params.randomize.num_ccs++] =
        atoi(pad_args.params->sval[i]);
    }
    ESP_LOGI(TAG, "Randomize %d CCs", action.params.randomize.num_ccs);
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    // Multi-CC support: cc_cycle <cc> <v1> <v2> ... [/ <cc2> <v1> <v2> ...]
    if (pad_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: pad <num> cc_cycle <cc> <v1> <v2> ... [/ <cc2> <v1> <v2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(pad_args.params->sval, pad_args.params->count,
        MULTI_CC_MODE_CC_CYCLE, &mcc) != 0) {
      return 1;
    }
    
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_CYCLE;
    action.params.control.num_ccs = mcc.num_ccs;
    action.params.control.num_cycle_steps = mcc.num_cycle_steps;
    action.params.control.current_index = 0;
    
    for (int i = 0; i < mcc.num_ccs; i++) {
      int cc_num = mcc.cc_numbers[i];
      action.params.control.cc_numbers[i] = cc_num;
      // Device-aware validation for each cycle value
      for (int j = 0; j < mcc.num_cycle_steps; j++) {
        int val = mcc.cycle_values[i][j];
        if (device) {
          val = assets_clamp_cc_value(device, cc_num, val);
          if (assets_cc_has_discrete_values(device, cc_num)) {
            val = assets_snap_to_discrete(device, cc_num, val);
          }
        }
        action.params.control.cycle_values[i][j] = val;
      }
    }
    ESP_LOGI(TAG, "CC Cycle with %d CC(s), %d steps", mcc.num_ccs, mcc.num_cycle_steps);
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "set_tempo") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> set_tempo <bpm>");
      return 1;
    }
    action = action_create_set_tempo(atoi(pad_args.params->sval[0]));
  }
  else if (strcmp(action_str, "tempo_inc") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_INCREMENT;
  }
  else if (strcmp(action_str, "tempo_dec") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_DECREMENT;
  }
  else if (strcmp(action_str, "play") == 0 || strcmp(action_str, "transport_play") == 0) {
    action = action_create_transport(VARIANT_PLAY);
  }
  else if (strcmp(action_str, "stop") == 0 || strcmp(action_str, "transport_stop") == 0) {
    action = action_create_transport(VARIANT_STOP);
  }
  else if (strcmp(action_str, "pause") == 0 || strcmp(action_str, "transport_pause") == 0) {
    action = action_create_transport(VARIANT_PAUSE);
  }
  else if (strcmp(action_str, "record") == 0 || strcmp(action_str, "transport_record") == 0) {
    action = action_create_transport(VARIANT_RECORD);
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_preset_inc();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_preset_dec();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_inc();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_dec();
  }
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "reset") == 0 || strcmp(action_str, "all_notes_off") == 0 ||
           strcmp(action_str, "all_sound_off") == 0) {
    action = action_create_reset();
  }
  else if (strcmp(action_str, "piano_pedal") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> piano_pedal <damper|sostenuto|soft|legato|hold2|64|66|67|68|69>");
      return 1;
    }
    uint8_t cc = parse_piano_pedal_arg(pad_args.params->sval[0]);
    if (cc == 0) {
      ESP_LOGE(TAG, "Unknown pedal '%s' (use damper/sostenuto/soft/legato/hold2 or 64/66/67/68/69)",
        pad_args.params->sval[0]);
      return 1;
    }
    action = action_create_piano_pedal(cc);
  }
  else if (strcmp(action_str, "sustain") == 0) {
    action = action_create_piano_pedal(64);
  }
  else if (strcmp(action_str, "sostenuto") == 0) {
    action = action_create_piano_pedal(66);
  }
  // Scene set
  else if (strcmp(action_str, "scene_set") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> scene_set <1-128>");
      return 1;
    }
    action.type = ACTION_SCENE;
    action.variant = VARIANT_SET;
    if (!parse_scene_number_1based(pad_args.params->sval[0],
        "Usage: pad <num> scene_set <1-128>",
        &action.params.target.number)) return 1;
  }
  // Touchwheel mode actions (consolidated). Canonical verb is "touchwheel
  // <hold|cycle> ..."; tw_mode_hold / tw_mode_cycle remain as legacy aliases.
  else if (strcmp(action_str, "touchwheel") == 0) {
    if (pad_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: pad <num> touchwheel <hold|cycle> ...");
      return 1;
    }
    if (!parse_touchwheel_action(pad_args.params->sval[0], &action,
        &pad_args.params->sval[1], pad_args.params->count - 1, "pad <num>")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_hold") == 0) {
    if (!parse_touchwheel_action("hold", &action,
        pad_args.params->sval, pad_args.params->count, "pad <num>")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_cycle") == 0) {
    if (!parse_touchwheel_action("cycle", &action,
        pad_args.params->sval, pad_args.params->count, "pad <num>")) return 1;
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
  esp_err_t ret = scene_assign_touchpad_action(scene_get_current_index(), pad, &action);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to pad %d", action_type_to_string(action.type), pad);
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGE(TAG, "Action '%s' is not valid for pad %d", action_type_to_string(action.type), pad);
  } else {
    ESP_LOGE(TAG, "Failed to assign action");
  }
  
  return 0;
}

// Command: button <left|right|both> <action_type> [params...]
static struct {
  struct arg_str *button_name;
  struct arg_str *action_type;
  struct arg_str *params;  // String args to allow "/" separator for multi-CC
  struct arg_end *end;
} button_args;

static int cmd_button(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &button_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, button_args.end, argv[0]);
    return 1;
  }
  
  const char* btn_name = button_args.button_name->sval[0];
  const char* action_str = button_args.action_type->sval[0];
  
  action_t action = {0};
  
  if (strcmp(action_str, "cc") == 0) {
    if (button_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: button <name> cc <cc> <val> [/ <cc2> <val2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(button_args.params->sval, button_args.params->count,
        MULTI_CC_MODE_CC, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_SET;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
    }
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    if (button_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: button <name> cc_hold <cc> <press> <release> [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(button_args.params->sval, button_args.params->count,
        MULTI_CC_MODE_CC_HOLD, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_HOLD;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
      action.params.control.values2[i] = mcc.values2[i];
    }
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (button_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: button <name> cc_cycle <cc> <v1> <v2> ... [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(button_args.params->sval, button_args.params->count,
        MULTI_CC_MODE_CC_CYCLE, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_CYCLE;
    action.params.control.num_ccs = mcc.num_ccs;
    action.params.control.num_cycle_steps = mcc.num_cycle_steps;
    action.params.control.current_index = 0;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      for (int j = 0; j < mcc.num_cycle_steps; j++) {
        action.params.control.cycle_values[i][j] = mcc.cycle_values[i][j];
      }
    }
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "set_tempo") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> set_tempo <bpm>");
      return 1;
    }
    action = action_create_set_tempo(atoi(button_args.params->sval[0]));
  }
  else if (strcmp(action_str, "tempo_inc") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_INCREMENT;
  }
  else if (strcmp(action_str, "tempo_dec") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_DECREMENT;
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_preset_inc();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_preset_dec();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_inc();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_dec();
  }
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "reset") == 0 || strcmp(action_str, "all_notes_off") == 0 ||
           strcmp(action_str, "all_sound_off") == 0) {
    action = action_create_reset();
  }
  else if (strcmp(action_str, "piano_pedal") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> piano_pedal <damper|sostenuto|soft|legato|hold2|64|66|67|68|69>");
      return 1;
    }
    uint8_t cc = parse_piano_pedal_arg(button_args.params->sval[0]);
    if (cc == 0) {
      ESP_LOGE(TAG, "Unknown pedal '%s' (use damper/sostenuto/soft/legato/hold2 or 64/66/67/68/69)",
        button_args.params->sval[0]);
      return 1;
    }
    action = action_create_piano_pedal(cc);
  }
  else if (strcmp(action_str, "sustain") == 0) {
    action = action_create_piano_pedal(64);
  }
  else if (strcmp(action_str, "sostenuto") == 0) {
    action = action_create_piano_pedal(66);
  }
  else if (strcmp(action_str, "pause") == 0 || strcmp(action_str, "transport_pause") == 0) {
    action = action_create_transport(VARIANT_PAUSE);
  }
  else if (strcmp(action_str, "record") == 0 || strcmp(action_str, "transport_record") == 0) {
    action = action_create_transport(VARIANT_RECORD);
  }
  else if (strcmp(action_str, "scene_set") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> scene_set <1-128>");
      return 1;
    }
    action.type = ACTION_SCENE;
    action.variant = VARIANT_SET;
    if (!parse_scene_number_1based(button_args.params->sval[0],
        "Usage: button <name> scene_set <1-128>",
        &action.params.target.number)) return 1;
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (button_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: button <name> note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(button_args.params->sval[0]);
    action.params.note.velocity = atoi(button_args.params->sval[1]);
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> note_off <note>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(button_args.params->sval[0]);
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> pc <program> (0-127 or 0-16383 with bank mode)");
      return 1;
    }
    action.type = ACTION_PRESET;
    action.variant = VARIANT_SET;
    action.params.preset.program = atoi(button_args.params->sval[0]);
  }
  else if (strcmp(action_str, "randomize") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> randomize <cc_num> [cc2] ...");
      return 1;
    }
    action.type = ACTION_RANDOMIZE;
    action.params.randomize.num_ccs = 0;
    for (int i = 0; i < button_args.params->count && action.params.randomize.num_ccs < 8; i++) {
      action.params.randomize.cc_numbers[action.params.randomize.num_ccs++] =
        atoi(button_args.params->sval[i]);
    }
  }
  // Touchwheel mode actions (consolidated). Canonical verb is "touchwheel
  // <hold|cycle> ..."; tw_mode_hold / tw_mode_cycle remain as legacy aliases.
  else if (strcmp(action_str, "touchwheel") == 0) {
    if (button_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: button <name> touchwheel <hold|cycle> ...");
      return 1;
    }
    if (!parse_touchwheel_action(button_args.params->sval[0], &action,
        &button_args.params->sval[1], button_args.params->count - 1, "button <name>")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_hold") == 0) {
    if (!parse_touchwheel_action("hold", &action,
        button_args.params->sval, button_args.params->count, "button <name>")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_cycle") == 0) {
    if (!parse_touchwheel_action("cycle", &action,
        button_args.params->sval, button_args.params->count, "button <name>")) return 1;
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
  uint8_t scene_idx = scene_get_current_index();
  esp_err_t ret = ESP_ERR_INVALID_ARG;
  
  if (strcmp(btn_name, "left") == 0) {
    ret = scene_assign_button_left(scene_idx, &action);
  } else if (strcmp(btn_name, "right") == 0) {
    ret = scene_assign_button_right(scene_idx, &action);
  } else if (strcmp(btn_name, "both") == 0) {
    ret = scene_assign_button_both(scene_idx, &action);
  } else {
    ESP_LOGE(TAG, "Unknown button: %s (use left, right, or both)", btn_name);
    return 1;
  }
  
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to %s button", action_type_to_string(action.type), btn_name);
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGE(TAG, "Action '%s' is not valid for %s button", action_type_to_string(action.type), btn_name);
  } else {
    ESP_LOGE(TAG, "Failed to assign action");
  }
  
  return 0;
}

// Command: bump <action_type> [params...]
static struct {
  struct arg_str *action_type;
  struct arg_str *params;  // String args to allow "/" separator for multi-CC
  struct arg_end *end;
} bump_args;

static int cmd_bump(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &bump_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, bump_args.end, argv[0]);
    return 1;
  }
  
  const char* action_str = bump_args.action_type->sval[0];
  
  action_t action = {0};
  
  if (strcmp(action_str, "cc") == 0) {
    if (bump_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: bump cc <cc> <val> [/ <cc2> <val2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(bump_args.params->sval, bump_args.params->count,
        MULTI_CC_MODE_CC, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_SET;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
    }
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    // Note: cc_hold will be rejected later by action_requires_hold check
    if (bump_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: bump cc_hold <cc> <press> <release> [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(bump_args.params->sval, bump_args.params->count,
        MULTI_CC_MODE_CC_HOLD, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_HOLD;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
      action.params.control.values2[i] = mcc.values2[i];
    }
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (bump_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: bump cc_cycle <cc> <v1> <v2> ... [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(bump_args.params->sval, bump_args.params->count,
        MULTI_CC_MODE_CC_CYCLE, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_CYCLE;
    action.params.control.num_ccs = mcc.num_ccs;
    action.params.control.num_cycle_steps = mcc.num_cycle_steps;
    action.params.control.current_index = 0;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      for (int j = 0; j < mcc.num_cycle_steps; j++) {
        action.params.control.cycle_values[i][j] = mcc.cycle_values[i][j];
      }
    }
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "set_tempo") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump set_tempo <bpm>");
      return 1;
    }
    action = action_create_set_tempo(atoi(bump_args.params->sval[0]));
  }
  else if (strcmp(action_str, "tempo_inc") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_INCREMENT;
  }
  else if (strcmp(action_str, "tempo_dec") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_DECREMENT;
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_preset_inc();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_preset_dec();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_inc();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_dec();
  }
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "reset") == 0 || strcmp(action_str, "all_notes_off") == 0 ||
           strcmp(action_str, "all_sound_off") == 0) {
    action = action_create_reset();
  }
  else if (strcmp(action_str, "piano_pedal") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump piano_pedal <damper|sostenuto|soft|legato|hold2|64|66|67|68|69>");
      return 1;
    }
    uint8_t cc = parse_piano_pedal_arg(bump_args.params->sval[0]);
    if (cc == 0) {
      ESP_LOGE(TAG, "Unknown pedal '%s' (use damper/sostenuto/soft/legato/hold2 or 64/66/67/68/69)",
        bump_args.params->sval[0]);
      return 1;
    }
    action = action_create_piano_pedal(cc);
  }
  else if (strcmp(action_str, "sustain") == 0) {
    action = action_create_piano_pedal(64);
  }
  else if (strcmp(action_str, "sostenuto") == 0) {
    action = action_create_piano_pedal(66);
  }
  else if (strcmp(action_str, "pause") == 0 || strcmp(action_str, "transport_pause") == 0) {
    action = action_create_transport(VARIANT_PAUSE);
  }
  else if (strcmp(action_str, "record") == 0 || strcmp(action_str, "transport_record") == 0) {
    action = action_create_transport(VARIANT_RECORD);
  }
  else if (strcmp(action_str, "scene_set") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump scene_set <1-128>");
      return 1;
    }
    action.type = ACTION_SCENE;
    action.variant = VARIANT_SET;
    if (!parse_scene_number_1based(bump_args.params->sval[0],
        "Usage: bump scene_set <1-128>",
        &action.params.target.number)) return 1;
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (bump_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: bump note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(bump_args.params->sval[0]);
    action.params.note.velocity = atoi(bump_args.params->sval[1]);
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump note_off <note>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(bump_args.params->sval[0]);
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump pc <program> (0-127 or 0-16383 with bank mode)");
      return 1;
    }
    action.type = ACTION_PRESET;
    action.variant = VARIANT_SET;
    action.params.preset.program = atoi(bump_args.params->sval[0]);
  }
  else if (strcmp(action_str, "randomize") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump randomize <cc_num> [cc2] ...");
      return 1;
    }
    action.type = ACTION_RANDOMIZE;
    action.params.randomize.num_ccs = 0;
    for (int i = 0; i < bump_args.params->count && action.params.randomize.num_ccs < 8; i++) {
      action.params.randomize.cc_numbers[action.params.randomize.num_ccs++] =
        atoi(bump_args.params->sval[i]);
    }
  }
  // Touchwheel mode actions (consolidated). Canonical verb is "touchwheel
  // <hold|cycle> ..."; tw_mode_hold / tw_mode_cycle remain as legacy aliases.
  // Hold variants will be rejected by the unified validator (bump has no
  // release event); cycle stays valid on bump.
  else if (strcmp(action_str, "touchwheel") == 0) {
    if (bump_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: bump touchwheel <hold|cycle> ...");
      return 1;
    }
    if (!parse_touchwheel_action(bump_args.params->sval[0], &action,
        &bump_args.params->sval[1], bump_args.params->count - 1, "bump")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_hold") == 0) {
    if (!parse_touchwheel_action("hold", &action,
        bump_args.params->sval, bump_args.params->count, "bump")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_cycle") == 0) {
    if (!parse_touchwheel_action("cycle", &action,
        bump_args.params->sval, bump_args.params->count, "bump")) return 1;
  }
  else if (strcmp(action_str, "none") == 0) {
    // Clear bump assignment
    action_t empty = {0};
    esp_err_t ret = scene_assign_bump(scene_get_current_index(), &empty);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Cleared bump assignment");
    }
    return 0;
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
  // Validate action against bump trigger
  if (!action_is_valid_for_trigger_for(&action, ACTION_TRIGGER_BUMP)) {
    ESP_LOGE(TAG, "'%s' is not valid for bump", action_str);
    return 1;
  }
  
  esp_err_t ret = scene_assign_bump(scene_get_current_index(), &action);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to bump", action_type_to_string(action.type));
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGE(TAG, "Action '%s' is not valid for bump", action_type_to_string(action.type));
  } else {
    ESP_LOGE(TAG, "Failed to assign action");
  }
  
  return 0;
}

// Command: expr_switch - Assign action(s) to expression switch mode
static struct {
  struct arg_str *action_type;
  struct arg_str *params;  // String args to allow "/" separator for multi-CC
  struct arg_end *end;
} expr_switch_args;

static int cmd_expr_switch(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_switch_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_switch_args.end, argv[0]);
    return 1;
  }
  
  const char* action_str = expr_switch_args.action_type->sval[0];
  
  action_t action = {0};
  
  if (strcmp(action_str, "cc") == 0) {
    if (expr_switch_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: expr_switch cc <cc> <val> [/ <cc2> <val2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(expr_switch_args.params->sval, expr_switch_args.params->count,
        MULTI_CC_MODE_CC, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_SET;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
    }
  }
  else if (strcmp(action_str, "cc_hold") == 0) {
    if (expr_switch_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: expr_switch cc_hold <cc> <press> <release> [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(expr_switch_args.params->sval, expr_switch_args.params->count,
        MULTI_CC_MODE_CC_HOLD, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_HOLD;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
      action.params.control.values2[i] = mcc.values2[i];
    }
  }
  else if (strcmp(action_str, "cc_cycle") == 0) {
    if (expr_switch_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: expr_switch cc_cycle <cc> <v1> <v2> ... [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(expr_switch_args.params->sval, expr_switch_args.params->count,
        MULTI_CC_MODE_CC_CYCLE, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_CYCLE;
    action.params.control.num_ccs = mcc.num_ccs;
    action.params.control.num_cycle_steps = mcc.num_cycle_steps;
    action.params.control.current_index = 0;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      for (int j = 0; j < mcc.num_cycle_steps; j++) {
        action.params.control.cycle_values[i][j] = mcc.cycle_values[i][j];
      }
    }
  }
  else if (strcmp(action_str, "tap_tempo") == 0) {
    action = action_create_tap_tempo();
  }
  else if (strcmp(action_str, "set_tempo") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch set_tempo <bpm>");
      return 1;
    }
    action = action_create_set_tempo(atoi(expr_switch_args.params->sval[0]));
  }
  else if (strcmp(action_str, "tempo_inc") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_INCREMENT;
  }
  else if (strcmp(action_str, "tempo_dec") == 0) {
    action.type = ACTION_TEMPO;
    action.variant = VARIANT_DECREMENT;
  }
  else if (strcmp(action_str, "program_next") == 0) {
    action = action_create_preset_inc();
  }
  else if (strcmp(action_str, "program_prev") == 0) {
    action = action_create_preset_dec();
  }
  else if (strcmp(action_str, "scene_next") == 0) {
    action = action_create_scene_inc();
  }
  else if (strcmp(action_str, "scene_prev") == 0) {
    action = action_create_scene_dec();
  }
  else if (strcmp(action_str, "confirm_pending") == 0) {
    action.type = ACTION_CONFIRM_PENDING;
  }
  else if (strcmp(action_str, "scene_set") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch scene_set <1-128>");
      return 1;
    }
    action.type = ACTION_SCENE;
    action.variant = VARIANT_SET;
    if (!parse_scene_number_1based(expr_switch_args.params->sval[0],
        "Usage: expr_switch scene_set <1-128>",
        &action.params.target.number)) return 1;
  }
  else if (strcmp(action_str, "piano_pedal") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch piano_pedal <damper|sostenuto|soft|legato|hold2|64|66|67|68|69>");
      return 1;
    }
    uint8_t cc = parse_piano_pedal_arg(expr_switch_args.params->sval[0]);
    if (cc == 0) {
      ESP_LOGE(TAG, "Unknown pedal '%s' (use damper/sostenuto/soft/legato/hold2 or 64/66/67/68/69)",
        expr_switch_args.params->sval[0]);
      return 1;
    }
    action = action_create_piano_pedal(cc);
  }
  else if (strcmp(action_str, "sustain") == 0) {
    action = action_create_piano_pedal(64);
  }
  else if (strcmp(action_str, "sostenuto") == 0) {
    action = action_create_piano_pedal(66);
  }
  else if (strcmp(action_str, "note_on") == 0) {
    if (expr_switch_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: expr_switch note_on <note> <velocity>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(expr_switch_args.params->sval[0]);
    action.params.note.velocity = atoi(expr_switch_args.params->sval[1]);
  }
  else if (strcmp(action_str, "note_off") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch note_off <note>");
      return 1;
    }
    action.type = ACTION_NOTE;
    action.params.note.note = atoi(expr_switch_args.params->sval[0]);
    action.params.note.velocity = 0;
  }
  else if (strcmp(action_str, "pc") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch pc <program>");
      return 1;
    }
    action.type = ACTION_PRESET;
    action.variant = VARIANT_SET;
    action.params.preset.program = atoi(expr_switch_args.params->sval[0]);
  }
  // Touchwheel mode actions
  // Touchwheel mode actions (consolidated). Canonical verb is "touchwheel
  // <hold|cycle> ..."; tw_mode_hold / tw_mode_cycle remain as legacy aliases.
  else if (strcmp(action_str, "touchwheel") == 0) {
    if (expr_switch_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: expr_switch touchwheel <hold|cycle> ...");
      return 1;
    }
    if (!parse_touchwheel_action(expr_switch_args.params->sval[0], &action,
        &expr_switch_args.params->sval[1], expr_switch_args.params->count - 1, "expr_switch")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_hold") == 0) {
    if (!parse_touchwheel_action("hold", &action,
        expr_switch_args.params->sval, expr_switch_args.params->count, "expr_switch")) return 1;
  }
  else if (strcmp(action_str, "tw_mode_cycle") == 0) {
    if (!parse_touchwheel_action("cycle", &action,
        expr_switch_args.params->sval, expr_switch_args.params->count, "expr_switch")) return 1;
  }
  else if (strcmp(action_str, "none") == 0) {
    // Clear expr_switch assignment
    action_t empty = {0};
    esp_err_t ret = scene_assign_expr_switch(scene_get_current_index(), &empty);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Cleared expr_switch assignment");
    }
    return 0;
  }
  else {
    ESP_LOGE(TAG, "Unknown action: %s. Type 'actions' for help", action_str);
    return 1;
  }
  
  esp_err_t ret = scene_assign_expr_switch(scene_get_current_index(), &action);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Assigned '%s' to expr_switch", action_type_to_string(action.type));
  } else if (ret == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGE(TAG, "Action '%s' is not valid for expr_switch", action_type_to_string(action.type));
  } else {
    ESP_LOGE(TAG, "Failed to assign action");
  }
  
  return 0;
}

// Command: on_load - Manage on_load actions
static struct {
  struct arg_str *subcommand;
  struct arg_str *params;  // String args to allow "/" separator for multi-CC
  struct arg_end *end;
} on_load_args;

static int cmd_on_load(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &on_load_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, on_load_args.end, argv[0]);
    return 1;
  }
  
  uint8_t scene_index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  if (!scene) {
    ESP_LOGE(TAG, "No current scene");
    return 1;
  }
  
  const char* subcmd = on_load_args.subcommand->sval[0];
  
  // Show current on_load actions
  if (strcmp(subcmd, "show") == 0 || strcmp(subcmd, "list") == 0) {
    ESP_LOGI(TAG, "On-load actions (%d/%d):", scene->num_on_load_actions, MAX_ON_LOAD_ACTIONS);
    if (scene->num_on_load_actions == 0) {
      ESP_LOGI(TAG, "  (none)");
    } else {
      for (int i = 0; i < scene->num_on_load_actions; i++) {
        ESP_LOGI(TAG, "  [%d] %s", i, action_type_to_string(scene->on_load[i].type));
      }
    }
    return 0;
  }
  
  // Clear all on_load actions
  if (strcmp(subcmd, "clear") == 0) {
    esp_err_t ret = scene_clear_on_load_actions(scene_index);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "Cleared all on_load actions");
    }
    return 0;
  }
  
  // Add an action
  action_t action = {0};
  
  if (strcmp(subcmd, "cc") == 0) {
    // Multi-CC support for on_load
    if (on_load_args.params->count < 2) {
      ESP_LOGE(TAG, "Usage: on_load cc <cc> <val> [/ <cc2> <val2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(on_load_args.params->sval, on_load_args.params->count,
        MULTI_CC_MODE_CC, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_SET;
    action.params.control.num_ccs = mcc.num_ccs;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      action.params.control.values[i] = mcc.values[i];
    }
  }
  else if (strcmp(subcmd, "cc_cycle") == 0) {
    // Multi-CC cycle support for on_load
    if (on_load_args.params->count < 3) {
      ESP_LOGE(TAG, "Usage: on_load cc_cycle <cc> <v1> <v2> ... [/ <cc2> ...]");
      return 1;
    }
    multi_cc_result_t mcc;
    if (parse_multi_cc(on_load_args.params->sval, on_load_args.params->count,
        MULTI_CC_MODE_CC_CYCLE, &mcc) != 0) {
      return 1;
    }
    action.type = ACTION_CONTROL;
    action.variant = VARIANT_CYCLE;
    action.params.control.num_ccs = mcc.num_ccs;
    action.params.control.num_cycle_steps = mcc.num_cycle_steps;
    action.params.control.current_index = 0;
    for (int i = 0; i < mcc.num_ccs; i++) {
      action.params.control.cc_numbers[i] = mcc.cc_numbers[i];
      for (int j = 0; j < mcc.num_cycle_steps; j++) {
        action.params.control.cycle_values[i][j] = mcc.cycle_values[i][j];
      }
    }
  }
  else if (strcmp(subcmd, "reset") == 0) {
    action = action_create_reset();
  }
  else if (strcmp(subcmd, "program_next") == 0) {
    action = action_create_preset_inc();
  }
  else if (strcmp(subcmd, "program_prev") == 0) {
    action = action_create_preset_dec();
  }
  else if (strcmp(subcmd, "scene_next") == 0) {
    action = action_create_scene_inc();
  }
  else if (strcmp(subcmd, "scene_prev") == 0) {
    action = action_create_scene_dec();
  }
  else if (strcmp(subcmd, "scene_set") == 0) {
    if (on_load_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: on_load scene_set <1-128>");
      return 1;
    }
    action.type = ACTION_SCENE;
    action.variant = VARIANT_SET;
    if (!parse_scene_number_1based(on_load_args.params->sval[0],
        "Usage: on_load scene_set <1-128>",
        &action.params.target.number)) return 1;
  }
  else if (strcmp(subcmd, "pc") == 0) {
    if (on_load_args.params->count < 1) {
      ESP_LOGE(TAG, "Usage: on_load pc <program>");
      return 1;
    }
    action.type = ACTION_PRESET;
    action.variant = VARIANT_SET;
    action.params.preset.program = atoi(on_load_args.params->sval[0]);
  }
  else {
    ESP_LOGE(TAG, "Usage: on_load <show|clear|action_type> [params...]");
    ESP_LOGI(TAG, "  show/list           - Show current on_load actions");
    ESP_LOGI(TAG, "  clear               - Remove all on_load actions");
    ESP_LOGI(TAG, "  reset               - Add reset action");
    ESP_LOGI(TAG, "  cc <cc> <val> ...   - Add CC action (multi-CC with /)");
    ESP_LOGI(TAG, "  cc_cycle <cc> <v1> <v2> ... - Add CC cycle");
    ESP_LOGI(TAG, "  pc <program>        - Add program change");
    ESP_LOGI(TAG, "(Hold actions like cc_hold/piano_pedal not allowed)");
    return 1;
  }
  
  esp_err_t ret = scene_add_on_load_action(scene_index, &action);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Added '%s' to on_load (now %d actions)",
      action_type_to_string(action.type), scene->num_on_load_actions);
  } else if (ret == ESP_ERR_NO_MEM) {
    ESP_LOGE(TAG, "On-load is full (max %d actions)", MAX_ON_LOAD_ACTIONS);
  }
  
  return 0;
}

// Command: actions - List available action types
static int cmd_actions(int argc, char **argv) {
  ESP_LOGI(TAG, "Available action types for pad/button/bump commands:");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "MIDI CC:");
  ESP_LOGI(TAG, "  cc <cc> <value>                  - Send CC on press (one-shot)");
  ESP_LOGI(TAG, "  cc_hold <cc> <press> <release>   - Send press val, release val");
  ESP_LOGI(TAG, "  cc_cycle <cc> <v1> <v2>...<v8>   - Cycle through 2-8 values");
  ESP_LOGI(TAG, "  randomize <cc> [cc2]...[cc8]     - Randomize 1-8 CCs (0-127)");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "MIDI Notes:");
  ESP_LOGI(TAG, "  note_on <note> <velocity>        - Send Note On");
  ESP_LOGI(TAG, "  note_off <note>                  - Send Note Off");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Program/Scene:");
  ESP_LOGI(TAG, "  pc <program>                     - Program change (0-127 or 0-16383)");
  ESP_LOGI(TAG, "  program_next / program_prev      - Inc/dec program");
  ESP_LOGI(TAG, "  scene_next / scene_prev          - Inc/dec scene");
  ESP_LOGI(TAG, "  scene_set <1-128>                - Set specific scene");
  ESP_LOGI(TAG, "  confirm_pending                  - Confirm pending change");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Tempo:");
  ESP_LOGI(TAG, "  tap_tempo                        - Tap to set BPM (moving average)");
  ESP_LOGI(TAG, "  set_tempo <bpm>                  - Set tempo (20-300)");
  ESP_LOGI(TAG, "  tempo_inc / tempo_dec            - +/- 1 BPM");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Transport:");
  ESP_LOGI(TAG, "  play                             - Toggle play/pause");
  ESP_LOGI(TAG, "  stop                             - Stop");
  ESP_LOGI(TAG, "  pause                            - Pause");
  ESP_LOGI(TAG, "  record                           - Toggle record/pause");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Utility:");
  ESP_LOGI(TAG, "  reset                            - CC123 + CC120 + System Reset");
  ESP_LOGI(TAG, "  piano_pedal <pedal>              - Hold pedal (damper/sostenuto/soft/legato/hold2)");
  ESP_LOGI(TAG, "    Legacy: sustain (CC64), sostenuto (CC66) -- still accepted");
  ESP_LOGI(TAG, "  none                             - Clear assignment");
  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Touchwheel Mode:");
  ESP_LOGI(TAG, "  touchwheel hold <press> <release|original>  - Mode on press, restore on release");
  ESP_LOGI(TAG, "  touchwheel cycle <m1> <m2>...<m8>           - Cycle through 2-8 modes");
  ESP_LOGI(TAG, "  (legacy aliases: tw_mode_hold, tw_mode_cycle)");
  ESP_LOGI(TAG, "    Modes: 0=pads 1=pc 2=cc 3=tempo 4=pitch_bend");
  ESP_LOGI(TAG, "           5=aftertouch 6=double_cc");
  
  return 0;
}

// Command: pc (set program number for current scene)
static struct {
  struct arg_int *program_num;
  struct arg_end *end;
} pc_args;

static int cmd_pc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &pc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, pc_args.end, argv[0]);
    return 1;
  }
  
  int prog = pc_args.program_num->ival[0];
  if (prog < 0 || prog > 127) {
    ESP_LOGE(TAG, "Program number must be 0-127");
    return 1;
  }
  
  scene_set_program_number(scene_get_current_index(), prog);
  ESP_LOGI(TAG, "Scene %d program number: %d", scene_get_current_index() + 1, prog);
  return 0;
}

// Command: note_channel (set note output MIDI channel for current scene)
static struct {
  struct arg_int *channel_num;
  struct arg_end *end;
} note_channel_args;

static int cmd_note_channel(int argc, char **argv) {
  // If no arguments, show current note channel
  if (argc == 1) {
    uint8_t setting = scene_get_note_channel_setting(scene_get_current_index());
    uint8_t effective = scene_get_note_channel(scene_get_current_index());
    if (setting == 0) {
      ESP_LOGI(TAG, "Scene %d note channel: default (effective: %d)",
        scene_get_current_index() + 1, effective);
    } else {
      ESP_LOGI(TAG, "Scene %d note channel: %d", scene_get_current_index() + 1, setting);
    }
    return 0;
  }

  int nerrors = arg_parse(argc, argv, (void **) &note_channel_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, note_channel_args.end, argv[0]);
    return 1;
  }
  
  int ch = note_channel_args.channel_num->ival[0];
  if (ch < 0 || ch > 16) {
    ESP_LOGE(TAG, "Note channel must be 0-16 (0 = use scene channel)");
    return 1;
  }
  
  esp_err_t ret = scene_set_note_channel(scene_get_current_index(), (uint8_t)ch);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set note channel");
    return 1;
  }
  
  if (ch == 0) {
    ESP_LOGI(TAG, "Scene %d note channel: default (scene channel)",
      scene_get_current_index() + 1);
  } else {
    ESP_LOGI(TAG, "Scene %d note channel: %d", scene_get_current_index() + 1, ch);
  }
  return 0;
}

// Command: expr_cc - Set expression CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} expr_cc_args;

static int cmd_expr_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = expr_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (expr_cc_args.cc_nums->ival[i] < 0 || expr_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->expression.cc_number = expr_cc_args.cc_nums->ival[0];
    scene->expression.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Expression CC: %d", scene->expression.cc_number);
  } else {
    scene->expression.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->expression.num_cc_numbers; i++) {
      scene->expression.cc_numbers[i] = expr_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Expression CCs: %d assigned", scene->expression.num_cc_numbers);
  }
  
  // Auto-enable expression mapping when CC is assigned
  if (!scene->expression.enabled) {
    scene->expression.enabled = true;
    ESP_LOGI(TAG, "Expression mapping auto-enabled");
  }
  
  return 0;
}

// Command: expr_curve - Set expression curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} expr_curve_args;

static int cmd_expr_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = expr_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->expression.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->expression.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->expression.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->expression.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->expression.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->expression.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->expression.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "Expression curve: %s", curve_type_to_string(scene->expression.curve.type));
  return 0;
}

// Command: expr_polarity - Set expression polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} expr_polarity_args;

static int cmd_expr_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = expr_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->expression.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->expression.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->expression.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Expression polarity: %s", pol);
  return 0;
}

// Command: expr_enable - Enable/disable expression
static struct {
  struct arg_str *state;
  struct arg_end *end;
} expr_enable_args;

static int cmd_expr_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = expr_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->expression.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->expression.max_value == 0) {
    scene->expression.min_value = 0;
    scene->expression.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "Expression: %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: expr_output - Set expression output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} expr_output_args;

static int cmd_expr_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = expr_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->expression.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->expression.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  // Auto-enable expression mapping when output type is set
  if (!scene->expression.enabled) {
    scene->expression.enabled = true;
    ESP_LOGI(TAG, "Expression mapping auto-enabled");
  }
  
  ESP_LOGI(TAG, "Expression output: %s", type);
  return 0;
}

// Command: expr_base_note - Set expression base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} expr_base_note_args;

static int cmd_expr_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = expr_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->expression.base_note = note;
  ESP_LOGI(TAG, "Expression base note: %d", note);
  return 0;
}

// Command: expr_note_range - Set expression note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} expr_note_range_args;

static int cmd_expr_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = expr_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->expression.note_range = range;
  ESP_LOGI(TAG, "Expression note range: %d semitones", range);
  return 0;
}

// Command: expr_velocity - Set expression note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} expr_velocity_args;

static int cmd_expr_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = expr_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->expression.velocity = vel;
  ESP_LOGI(TAG, "Expression velocity: %d", vel);
  return 0;
}

// Command: expr_mode - Set expression jack mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} expr_mode_args;

static int cmd_expr_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expr_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expr_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = expr_mode_args.mode->sval[0];
  expression_mode_t mode;
  
  if (strcmp(mode_str, "none") == 0 || strcmp(mode_str, "off") == 0) {
    mode = EXPRESSION_MODE_NONE;
  } else if (strcmp(mode_str, "expression") == 0 || strcmp(mode_str, "expr") == 0) {
    mode = EXPRESSION_MODE_PEDAL;
  } else if (strcmp(mode_str, "sustain") == 0) {
    mode = EXPRESSION_MODE_SUSTAIN;
  } else if (strcmp(mode_str, "sostenuto") == 0) {
    mode = EXPRESSION_MODE_SOSTENUTO;
  } else if (strcmp(mode_str, "gate") == 0) {
    mode = EXPRESSION_MODE_GATE;
  } else if (strcmp(mode_str, "switch") == 0) {
    mode = EXPRESSION_MODE_SWITCH;
  } else {
    ESP_LOGE(TAG, "Unknown mode (use: none, expression, sustain, sostenuto, gate, switch)");
    return 1;
  }
  
  uint8_t scene_idx = scene_get_current_index();
  scene_set_expression_mode(scene_idx, mode);
  
  return 0;
}

// Command: cv_cc - Set CV CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} cv_cc_args;

static int cmd_cv_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = cv_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (cv_cc_args.cc_nums->ival[i] < 0 || cv_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->cv.cc_number = cv_cc_args.cc_nums->ival[0];
    scene->cv.num_cc_numbers = 0;
    ESP_LOGI(TAG, "CV CC: %d", scene->cv.cc_number);
  } else {
    scene->cv.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->cv.num_cc_numbers; i++) {
      scene->cv.cc_numbers[i] = cv_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "CV CCs: %d assigned", scene->cv.num_cc_numbers);
  }
  
  // Auto-enable CV mapping when CC is assigned
  if (!scene->cv.enabled) {
    scene->cv.enabled = true;
    ESP_LOGI(TAG, "CV mapping auto-enabled");
  }
  
  return 0;
}

// Command: cv_curve - Set CV curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} cv_curve_args;

static int cmd_cv_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = cv_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->cv.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->cv.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->cv.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->cv.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->cv.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->cv.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->cv.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "CV curve: %s", curve_type_to_string(scene->cv.curve.type));
  return 0;
}

// Command: cv_polarity - Set CV polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} cv_polarity_args;

static int cmd_cv_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = cv_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->cv.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->cv.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->cv.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "CV polarity: %s", pol);
  return 0;
}

// Command: cv_enable - Enable/disable CV
static struct {
  struct arg_str *state;
  struct arg_end *end;
} cv_enable_args;

static int cmd_cv_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = cv_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->cv.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->cv.max_value == 0) {
    scene->cv.min_value = 0;
    scene->cv.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "CV: %s", enable ? "enabled" : "disabled");
  
  return 0;
}

// Command: cv_output - Set CV output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} cv_output_args;

static int cmd_cv_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = cv_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->cv.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->cv.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  // Auto-enable CV mapping when output type is set
  if (!scene->cv.enabled) {
    scene->cv.enabled = true;
    ESP_LOGI(TAG, "CV mapping auto-enabled");
  }
  
  ESP_LOGI(TAG, "CV output: %s", type);
  return 0;
}

// Command: cv_base_note - Set CV base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} cv_base_note_args;

static int cmd_cv_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = cv_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->cv.base_note = note;
  ESP_LOGI(TAG, "CV base note: %d", note);
  return 0;
}

// Command: cv_note_range - Set CV note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} cv_note_range_args;

static int cmd_cv_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = cv_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->cv.note_range = range;
  ESP_LOGI(TAG, "CV note range: %d semitones", range);
  return 0;
}

// Command: cv_velocity - Set CV note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} cv_velocity_args;

static int cmd_cv_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = cv_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->cv.velocity = vel;
  ESP_LOGI(TAG, "CV velocity: %d", vel);
  return 0;
}

// Command: cv_input_mode - Set CV input mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} cv_input_mode_args;

static int cmd_cv_input_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_input_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_input_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* mode_str = cv_input_mode_args.mode->sval[0];
  input_mode_t mode;
  
  if (strcmp(mode_str, "none") == 0 || strcmp(mode_str, "off") == 0) {
    mode = INPUT_MODE_NONE;
  } else if (strcmp(mode_str, "cv") == 0) {
    mode = INPUT_MODE_CV;
  } else if (strcmp(mode_str, "clock_sync") == 0 || strcmp(mode_str, "clock") == 0 || strcmp(mode_str, "sync") == 0) {
    // clock_sync mode can only be set via clock_source sync
    ESP_LOGE(TAG, "Cannot set clock_sync directly - use 'clock_source sync' instead");
    return 1;
  } else if (strcmp(mode_str, "audio") == 0) {
    mode = INPUT_MODE_AUDIO;
  } else if (strcmp(mode_str, "note") == 0) {
    mode = INPUT_MODE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown CV input mode (use: none, cv, audio, note)");
    return 1;
  }
  
  scene_set_cv_input_mode(scene_get_current_index(), mode);
  
  // Actually enable the hardware mode
  esp_err_t ret = input_set_mode(mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable input mode: %s", esp_err_to_name(ret));
    return 1;
  }
  
  const char* mode_name = (mode == INPUT_MODE_CV) ? "CV" :
                          (mode == INPUT_MODE_CLOCK_SYNC) ? "Clock Sync" :
                          (mode == INPUT_MODE_AUDIO) ? "Audio" : "Note";
  ESP_LOGI(TAG, "CV input mode: %s", mode_name);
  return 0;
}

// Helper to parse velocity mode string
static velocity_mode_t parse_velocity_mode(const char* mode_str, bool* valid) {
  *valid = true;
  if (strcmp(mode_str, "fixed") == 0) return VELOCITY_MODE_FIXED;
  if (strcmp(mode_str, "gate") == 0 || strcmp(mode_str, "gate_voltage") == 0) return VELOCITY_MODE_GATE_VOLTAGE;
  if (strcmp(mode_str, "touchwheel") == 0 || strcmp(mode_str, "tw") == 0) return VELOCITY_MODE_TOUCHWHEEL;
  *valid = false;
  return VELOCITY_MODE_FIXED;
}

static const char* velocity_mode_name(velocity_mode_t mode) {
  switch (mode) {
    case VELOCITY_MODE_FIXED: return "Fixed";
    case VELOCITY_MODE_GATE_VOLTAGE: return "Gate Voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "Touchwheel";
    default: return "Unknown";
  }
}

// Command: cv_velocity_mode - Set CV NOTE mode velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} cv_velocity_mode_args;

static int cmd_cv_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  bool valid;
  velocity_mode_t mode = parse_velocity_mode(cv_velocity_mode_args.mode->sval[0], &valid);
  if (!valid) {
    ESP_LOGE(TAG, "Unknown velocity mode (use: fixed, gate_voltage, touchwheel)");
    return 1;
  }
  
  scene_set_cv_velocity_mode(scene_get_current_index(), mode);
  ESP_LOGI(TAG, "CV velocity mode: %s", velocity_mode_name(mode));
  return 0;
}

// Command: cv_note_velocity - Set CV NOTE input mode fixed velocity value
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} cv_note_velocity_args;

static int cmd_cv_note_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &cv_note_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cv_note_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = cv_note_velocity_args.velocity->ival[0];
  if (vel < 1 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 1-127");
    return 1;
  }
  
  scene_set_cv_velocity(scene_get_current_index(), (uint8_t)vel);
  ESP_LOGI(TAG, "CV NOTE input velocity: %d", vel);
  return 0;
}

// Command: expression_velocity_mode - Set expression note output velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} expression_velocity_mode_args;

static int cmd_expression_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &expression_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, expression_velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  bool valid;
  velocity_mode_t mode = parse_velocity_mode(expression_velocity_mode_args.mode->sval[0], &valid);
  if (!valid) {
    ESP_LOGE(TAG, "Unknown velocity mode (use: fixed, gate_voltage, touchwheel)");
    return 1;
  }
  
  scene_set_expression_velocity_mode(scene_get_current_index(), mode);
  ESP_LOGI(TAG, "Expression velocity mode: %s", velocity_mode_name(mode));
  return 0;
}

// Command: proximity_velocity_mode - Set proximity note output velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} proximity_velocity_mode_args;

static int cmd_proximity_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  bool valid;
  velocity_mode_t mode = parse_velocity_mode(proximity_velocity_mode_args.mode->sval[0], &valid);
  if (!valid) {
    ESP_LOGE(TAG, "Unknown velocity mode (use: fixed, gate_voltage, touchwheel)");
    return 1;
  }
  
  scene_set_proximity_velocity_mode(scene_get_current_index(), mode);
  ESP_LOGI(TAG, "Proximity velocity mode: %s", velocity_mode_name(mode));
  return 0;
}

// Command: als_velocity_mode - Set ALS note output velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} als_velocity_mode_args;

static int cmd_als_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_velocity_mode_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  bool valid;
  velocity_mode_t mode = parse_velocity_mode(als_velocity_mode_args.mode->sval[0], &valid);
  if (!valid) {
    ESP_LOGE(TAG, "Unknown velocity mode (use: fixed, gate_voltage, touchwheel)");
    return 1;
  }
  
  scene_set_als_velocity_mode(scene_get_current_index(), mode);
  ESP_LOGI(TAG, "ALS velocity mode: %s", velocity_mode_name(mode));
  return 0;
}

// Command: clock_source - Set tempo clock source
static struct {
  struct arg_str *source;
  struct arg_end *end;
} clock_source_args;

static int cmd_clock_source(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &clock_source_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, clock_source_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* src_str = clock_source_args.source->sval[0];
  tempo_clock_source_t source;
  
  if (strcmp(src_str, "internal") == 0 || strcmp(src_str, "int") == 0) {
    source = CLOCK_SOURCE_INTERNAL;
  } else if (strcmp(src_str, "midi") == 0) {
    source = CLOCK_SOURCE_MIDI;
  } else if (strcmp(src_str, "sync") == 0) {
    source = CLOCK_SOURCE_SYNC;
  } else {
    ESP_LOGE(TAG, "Unknown clock source (use: internal, midi, sync)");
    return 1;
  }
  
  scene_set_clock_source(scene_get_current_index(), source);
  
  const char* source_name = (source == CLOCK_SOURCE_INTERNAL) ? "Internal" :
                            (source == CLOCK_SOURCE_MIDI) ? "MIDI" : "Sync";
  ESP_LOGI(TAG, "Clock source: %s", source_name);
  return 0;
}

// Command: beat_divider - Set beat division for this scene
// Command: bpm - Set scene tempo
static struct {
  struct arg_int *bpm;
  struct arg_end *end;
} scene_bpm_args;

static int cmd_scene_bpm(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &scene_bpm_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, scene_bpm_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int bpm = scene_bpm_args.bpm->ival[0];
  if (bpm < 20 || bpm > 300) {
    ESP_LOGE(TAG, "BPM must be 20-300");
    return 1;
  }
  
  esp_err_t err = scene_set_bpm(scene_get_current_index(), (uint16_t)bpm);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Scene BPM can only be modified in programming mode");
    ESP_LOGI(TAG, "Use 'tempo bpm %d' for a live tempo change (won't persist)", bpm);
    return 1;
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set BPM");
    return 1;
  }
  
  ESP_LOGI(TAG, "Scene BPM: %d (persisted)", bpm);
  return 0;
}

// Command: beat_divider - Set beat division for this scene
static struct {
  struct arg_str *divider;
  struct arg_end *end;
} beat_divider_args;

static int cmd_beat_divider(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &beat_divider_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, beat_divider_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* div_str = beat_divider_args.divider->sval[0];
  tempo_note_divider_t divider;
  
  if (strcmp(div_str, "quarter") == 0 || strcmp(div_str, "q") == 0) {
    divider = DIVIDER_QUARTER;
  } else if (strcmp(div_str, "eighth") == 0 || strcmp(div_str, "8th") == 0) {
    divider = DIVIDER_EIGHTH;
  } else if (strcmp(div_str, "sixteenth") == 0 || strcmp(div_str, "16th") == 0) {
    divider = DIVIDER_SIXTEENTH;
  } else {
    ESP_LOGE(TAG, "Unknown beat divider (use: quarter, eighth, sixteenth)");
    return 1;
  }
  
  scene_set_beat_divider(scene_get_current_index(), divider);
  
  const char* div_name = (divider == DIVIDER_QUARTER) ? "Quarter" :
                         (divider == DIVIDER_EIGHTH) ? "Eighth" : "Sixteenth";
  ESP_LOGI(TAG, "Beat divider: %s", div_name);
  return 0;
}

// Command: time_sig - Set time signature
static struct {
  struct arg_int *numerator;
  struct arg_int *denominator;
  struct arg_end *end;
} time_sig_args;

static int cmd_time_sig(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &time_sig_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, time_sig_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int num = time_sig_args.numerator->ival[0];
  int denom = time_sig_args.denominator->ival[0];
  
  if (num < 1 || num > 16 || denom < 1 || denom > 16) {
    ESP_LOGE(TAG, "Time signature values must be 1-16");
    return 1;
  }
  
  scene_set_time_signature(scene_get_current_index(), num, denom);
  
  ESP_LOGI(TAG, "Time signature: %d/%d", num, denom);
  return 0;
}

// Command: use_transport - Set whether transport controls affect animation
static struct {
  struct arg_str *on_off;
  struct arg_end *end;
} use_transport_args;

static int cmd_use_transport(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &use_transport_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, use_transport_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* val = use_transport_args.on_off->sval[0];
  bool use_transport;
  
  if (strcmp(val, "on") == 0 || strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
    use_transport = true;
  } else if (strcmp(val, "off") == 0 || strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
    use_transport = false;
  } else {
    ESP_LOGE(TAG, "Invalid value (use: on/off)");
    return 1;
  }
  
  scene_set_use_transport(scene_get_current_index(), use_transport);
  
  ESP_LOGI(TAG, "use_transport: %s (animation %s)", 
           use_transport ? "on" : "off",
           use_transport ? "follows transport" : "always runs");
  return 0;
}

// Command: send_clock - Set whether scene sends MIDI clock
static struct {
  struct arg_str *on_off;
  struct arg_end *end;
} send_clock_args;

static int cmd_send_clock(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &send_clock_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, send_clock_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* val = send_clock_args.on_off->sval[0];
  bool send_clock;
  
  if (strcmp(val, "on") == 0 || strcmp(val, "true") == 0 || strcmp(val, "1") == 0) {
    send_clock = true;
  } else if (strcmp(val, "off") == 0 || strcmp(val, "false") == 0 || strcmp(val, "0") == 0) {
    send_clock = false;
  } else {
    ESP_LOGE(TAG, "Invalid value (use: on/off)");
    return 1;
  }
  
  scene_set_send_clock(scene_get_current_index(), send_clock);
  
  ESP_LOGI(TAG, "send_clock: %s (scene %s MIDI clock)",
    send_clock ? "on" : "off",
    send_clock ? "sends" : "does not send");
  return 0;
}

// Command: proximity_cc - Set proximity CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} proximity_cc_args;

static int cmd_proximity_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = proximity_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (proximity_cc_args.cc_nums->ival[i] < 0 || proximity_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->proximity.cc_number = proximity_cc_args.cc_nums->ival[0];
    scene->proximity.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Proximity CC: %d", scene->proximity.cc_number);
  } else {
    scene->proximity.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->proximity.num_cc_numbers; i++) {
      scene->proximity.cc_numbers[i] = proximity_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Proximity CCs: %d assigned", scene->proximity.num_cc_numbers);
  }
  
  // Auto-enable proximity mapping when CC is assigned
  if (!scene->proximity.enabled) {
    scene->proximity.enabled = true;
    ESP_LOGI(TAG, "Proximity mapping auto-enabled");
  }
  
  return 0;
}

// Command: proximity_curve - Set proximity curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} proximity_curve_args;

static int cmd_proximity_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = proximity_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->proximity.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->proximity.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->proximity.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->proximity.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->proximity.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->proximity.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->proximity.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "Proximity curve: %s", curve_type_to_string(scene->proximity.curve.type));
  return 0;
}

// Command: proximity_polarity - Set proximity polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} proximity_polarity_args;

static int cmd_proximity_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = proximity_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->proximity.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->proximity.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->proximity.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "Proximity polarity: %s", pol);
  return 0;
}

// Command: proximity_enable - Enable/disable proximity
static struct {
  struct arg_str *state;
  struct arg_end *end;
} proximity_enable_args;

static int cmd_proximity_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = proximity_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->proximity.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->proximity.max_value == 0) {
    scene->proximity.min_value = 0;
    scene->proximity.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "Proximity: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: proximity_output - Set proximity output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} proximity_output_args;

static int cmd_proximity_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = proximity_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->proximity.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->proximity.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  // Auto-enable proximity mapping when output type is set
  if (!scene->proximity.enabled) {
    scene->proximity.enabled = true;
    ESP_LOGI(TAG, "Proximity mapping auto-enabled");
  }
  
  ESP_LOGI(TAG, "Proximity output: %s", type);
  return 0;
}

// Command: proximity_base_note - Set proximity base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} proximity_base_note_args;

static int cmd_proximity_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = proximity_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->proximity.base_note = note;
  ESP_LOGI(TAG, "Proximity base note: %d", note);
  return 0;
}

// Command: proximity_note_range - Set proximity note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} proximity_note_range_args;

static int cmd_proximity_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = proximity_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->proximity.note_range = range;
  ESP_LOGI(TAG, "Proximity note range: %d semitones", range);
  return 0;
}

// Command: proximity_velocity - Set proximity note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} proximity_velocity_args;

static int cmd_proximity_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &proximity_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, proximity_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = proximity_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->proximity.velocity = vel;
  ESP_LOGI(TAG, "Proximity velocity: %d", vel);
  return 0;
}

// Command: als_cc - Set ALS CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} als_cc_args;

static int cmd_als_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = als_cc_args.cc_nums->count;
  for (int i = 0; i < count; i++) {
    if (als_cc_args.cc_nums->ival[i] < 0 || als_cc_args.cc_nums->ival[i] > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    scene->als.cc_number = als_cc_args.cc_nums->ival[0];
    scene->als.num_cc_numbers = 0;
    ESP_LOGI(TAG, "ALS CC: %d", scene->als.cc_number);
  } else {
    scene->als.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->als.num_cc_numbers; i++) {
      scene->als.cc_numbers[i] = als_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "ALS CCs: %d assigned", scene->als.num_cc_numbers);
  }
  
  // Auto-enable ALS mapping when CC is assigned
  if (!scene->als.enabled) {
    scene->als.enabled = true;
    ESP_LOGI(TAG, "ALS mapping auto-enabled");
  }
  
  return 0;
}

// Command: als_curve - Set ALS curve
static struct {
  struct arg_str *curve_name;
  struct arg_end *end;
} als_curve_args;

static int cmd_als_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_curve_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* curve = als_curve_args.curve_name->sval[0];
  
  if (strcmp(curve, "linear") == 0) {
    scene->als.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->als.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->als.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->als.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->als.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->als.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->als.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve");
    ESP_LOGE(TAG, "Available: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }
  
  ESP_LOGI(TAG, "ALS curve: %s", curve_type_to_string(scene->als.curve.type));
  return 0;
}

// Command: als_polarity - Set ALS polarity
static struct {
  struct arg_str *polarity_name;
  struct arg_end *end;
} als_polarity_args;

static int cmd_als_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_polarity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* pol = als_polarity_args.polarity_name->sval[0];
  
  if (strcmp(pol, "unipolar") == 0 || strcmp(pol, "uni") == 0) {
    scene->als.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0 || strcmp(pol, "bi") == 0) {
    scene->als.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0 || strcmp(pol, "inv") == 0) {
    scene->als.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity (use: unipolar, bipolar, inverted)");
    return 1;
  }
  
  ESP_LOGI(TAG, "ALS polarity: %s", pol);
  return 0;
}

// Command: als_enable - Enable/disable ALS
static struct {
  struct arg_str *state;
  struct arg_end *end;
} als_enable_args;

static int cmd_als_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_enable_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* state_str = als_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);
  
  scene->als.enabled = enable;
  
  // If enabling and range is invalid, set defaults
  if (enable && scene->als.max_value == 0) {
    scene->als.min_value = 0;
    scene->als.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }
  
  ESP_LOGI(TAG, "ALS: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: als_output - Set ALS output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} als_output_args;

static int cmd_als_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_output_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  const char* type = als_output_args.output_type->sval[0];
  
  if (strcmp(type, "cc") == 0) {
    scene->als.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->als.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }
  
  // Auto-enable ALS mapping when output type is set
  if (!scene->als.enabled) {
    scene->als.enabled = true;
    ESP_LOGI(TAG, "ALS mapping auto-enabled");
  }
  
  ESP_LOGI(TAG, "ALS output: %s", type);
  return 0;
}

// Command: als_base_note - Set ALS base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} als_base_note_args;

static int cmd_als_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_base_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int note = als_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }
  
  scene->als.base_note = note;
  ESP_LOGI(TAG, "ALS base note: %d", note);
  return 0;
}

// Command: als_note_range - Set ALS note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} als_note_range_args;

static int cmd_als_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_note_range_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int range = als_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Note range must be 1-127 semitones");
    return 1;
  }
  
  scene->als.note_range = range;
  ESP_LOGI(TAG, "ALS note range: %d semitones", range);
  return 0;
}

// Command: als_velocity - Set ALS note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} als_velocity_args;

static int cmd_als_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &als_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, als_velocity_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int vel = als_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }
  
  scene->als.velocity = vel;
  ESP_LOGI(TAG, "ALS velocity: %d", vel);
  return 0;
}

//=============================================================================
// LFO1 Commands
//=============================================================================

// Command: lfo1_enable - Enable/disable LFO1 MIDI output
static struct {
  struct arg_str *state;
  struct arg_end *end;
} lfo1_enable_args;

static int cmd_lfo1_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_enable_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* state_str = lfo1_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);

  scene->lfo1.enabled = enable;

  if (enable && scene->lfo1.max_value == 0) {
    scene->lfo1.min_value = 0;
    scene->lfo1.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }

  ESP_LOGI(TAG, "LFO1 MIDI output: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: lfo1_cc - Set LFO1 CC number(s)
static struct {
  struct arg_int *cc;
  struct arg_end *end;
} lfo1_cc_args;

static int cmd_lfo1_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_cc_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int count = lfo1_cc_args.cc->count;
  if (count < 1 || count > MAX_MULTI_CC) {
    ESP_LOGE(TAG, "Provide 1-%d CC numbers", MAX_MULTI_CC);
    return 1;
  }

  scene->lfo1.num_cc_numbers = count;
  for (int i = 0; i < count; i++) {
    int cc = lfo1_cc_args.cc->ival[i];
    if (cc < 0 || cc > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
    scene->lfo1.cc_numbers[i] = (uint8_t)cc;
  }
  scene->lfo1.cc_number = scene->lfo1.cc_numbers[0];

  char cc_buf[32];
  format_cc_list(&scene->lfo1, cc_buf, sizeof(cc_buf));
  ESP_LOGI(TAG, "LFO1 CC: %s", cc_buf);
  return 0;
}

// Command: lfo1_curve - Set LFO1 curve
static struct {
  struct arg_str *curve;
  struct arg_end *end;
} lfo1_curve_args;

static int cmd_lfo1_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_curve_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* curve = lfo1_curve_args.curve->sval[0];

  if (strcmp(curve, "linear") == 0) {
    scene->lfo1.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->lfo1.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->lfo1.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->lfo1.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->lfo1.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->lfo1.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->lfo1.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve. Use: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }

  ESP_LOGI(TAG, "LFO1 curve: %s", curve);
  return 0;
}

// Command: lfo1_polarity - Set LFO1 polarity
static struct {
  struct arg_str *polarity;
  struct arg_end *end;
} lfo1_polarity_args;

static int cmd_lfo1_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_polarity_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* pol = lfo1_polarity_args.polarity->sval[0];

  if (strcmp(pol, "unipolar") == 0) {
    scene->lfo1.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0) {
    scene->lfo1.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0) {
    scene->lfo1.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity. Use: unipolar, bipolar, inverted");
    return 1;
  }

  ESP_LOGI(TAG, "LFO1 polarity: %s", pol);
  return 0;
}

// Command: lfo1_output - Set LFO1 output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} lfo1_output_args;

static int cmd_lfo1_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_output_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* type = lfo1_output_args.output_type->sval[0];

  if (strcmp(type, "cc") == 0) {
    scene->lfo1.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->lfo1.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type. Use: cc, note");
    return 1;
  }

  ESP_LOGI(TAG, "LFO1 output: %s", type);
  return 0;
}

// Command: lfo1_base_note - Set LFO1 base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} lfo1_base_note_args;

static int cmd_lfo1_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_base_note_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int note = lfo1_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }

  scene->lfo1.base_note = (uint8_t)note;
  ESP_LOGI(TAG, "LFO1 base note: %d", note);
  return 0;
}

// Command: lfo1_note_range - Set LFO1 note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} lfo1_note_range_args;

static int cmd_lfo1_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_note_range_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int range = lfo1_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Range must be 1-127");
    return 1;
  }

  scene->lfo1.note_range = (uint8_t)range;
  ESP_LOGI(TAG, "LFO1 note range: %d semitones", range);
  return 0;
}

// Command: lfo1_velocity - Set LFO1 note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} lfo1_velocity_args;

static int cmd_lfo1_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_velocity_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int vel = lfo1_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }

  scene->lfo1.velocity = (uint8_t)vel;
  ESP_LOGI(TAG, "LFO1 velocity: %d", vel);
  return 0;
}

// Command: lfo1_velocity_mode - Set LFO1 velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} lfo1_velocity_mode_args;

static int cmd_lfo1_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_velocity_mode_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* mode = lfo1_velocity_mode_args.mode->sval[0];

  if (strcmp(mode, "fixed") == 0) {
    scene->lfo1_velocity_mode = VELOCITY_MODE_FIXED;
  } else if (strcmp(mode, "gate_voltage") == 0 || strcmp(mode, "gate") == 0) {
    scene->lfo1_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
  } else if (strcmp(mode, "touchwheel") == 0 || strcmp(mode, "tw") == 0) {
    scene->lfo1_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: fixed, gate_voltage, touchwheel");
    return 1;
  }

  ESP_LOGI(TAG, "LFO1 velocity mode: %s", mode);
  return 0;
}

// Command: lfo1_repeat - Set LFO1 repeat mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} lfo1_repeat_args;

static int cmd_lfo1_repeat(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_repeat_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_repeat_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* mode = lfo1_repeat_args.mode->sval[0];
  bool repeat = (strcmp(mode, "loop") == 0 || strcmp(mode, "on") == 0 ||
                 strcmp(mode, "1") == 0 || strcmp(mode, "true") == 0);

  scene->lfo1_config.repeat = repeat;
  lfo_set_repeat(0, repeat);  // Also update running LFO

  ESP_LOGI(TAG, "LFO1 repeat: %s", repeat ? "loop" : "one-shot");
  return 0;
}

// Command: lfo1_trigger - Set LFO1 trigger timing
static struct {
  struct arg_str *timing;
  struct arg_end *end;
} lfo1_trigger_args;

static int cmd_lfo1_trigger(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo1_trigger_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo1_trigger_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  lfo_trigger_timing_t timing = lfo_trigger_timing_from_string(lfo1_trigger_args.timing->sval[0]);

  scene->lfo1_config.trigger_timing = timing;
  lfo_set_trigger_timing(0, timing);  // Also update running LFO

  ESP_LOGI(TAG, "LFO1 trigger timing: %s", lfo_trigger_timing_to_string(timing));
  return 0;
}

//=============================================================================
// LFO2 Commands
//=============================================================================

// Command: lfo2_enable - Enable/disable LFO2 MIDI output
static struct {
  struct arg_str *state;
  struct arg_end *end;
} lfo2_enable_args;

static int cmd_lfo2_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_enable_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* state_str = lfo2_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);

  scene->lfo2.enabled = enable;

  if (enable && scene->lfo2.max_value == 0) {
    scene->lfo2.min_value = 0;
    scene->lfo2.max_value = 127;
    ESP_LOGI(TAG, "Initialized range to 0-127");
  }

  ESP_LOGI(TAG, "LFO2 MIDI output: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: lfo2_cc - Set LFO2 CC number(s)
static struct {
  struct arg_int *cc;
  struct arg_end *end;
} lfo2_cc_args;

static int cmd_lfo2_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_cc_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int count = lfo2_cc_args.cc->count;
  if (count < 1 || count > MAX_MULTI_CC) {
    ESP_LOGE(TAG, "Provide 1-%d CC numbers", MAX_MULTI_CC);
    return 1;
  }

  scene->lfo2.num_cc_numbers = count;
  for (int i = 0; i < count; i++) {
    int cc = lfo2_cc_args.cc->ival[i];
    if (cc < 0 || cc > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
    scene->lfo2.cc_numbers[i] = (uint8_t)cc;
  }
  scene->lfo2.cc_number = scene->lfo2.cc_numbers[0];

  char cc_buf[32];
  format_cc_list(&scene->lfo2, cc_buf, sizeof(cc_buf));
  ESP_LOGI(TAG, "LFO2 CC: %s", cc_buf);
  return 0;
}

// Command: lfo2_curve - Set LFO2 curve
static struct {
  struct arg_str *curve;
  struct arg_end *end;
} lfo2_curve_args;

static int cmd_lfo2_curve(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_curve_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_curve_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* curve = lfo2_curve_args.curve->sval[0];

  if (strcmp(curve, "linear") == 0) {
    scene->lfo2.curve = curve_create(CURVE_LINEAR);
  } else if (strcmp(curve, "exp") == 0 || strcmp(curve, "exponential") == 0) {
    scene->lfo2.curve = curve_create(CURVE_EXPONENTIAL);
  } else if (strcmp(curve, "log") == 0 || strcmp(curve, "logarithmic") == 0) {
    scene->lfo2.curve = curve_create(CURVE_LOGARITHMIC);
  } else if (strcmp(curve, "s") == 0 || strcmp(curve, "s_curve") == 0) {
    scene->lfo2.curve = curve_create(CURVE_S_CURVE);
  } else if (strcmp(curve, "quad") == 0 || strcmp(curve, "quadratic") == 0) {
    scene->lfo2.curve = curve_create(CURVE_QUADRATIC);
  } else if (strcmp(curve, "sqrt") == 0) {
    scene->lfo2.curve = curve_create(CURVE_SQUARE_ROOT);
  } else if (strcmp(curve, "sine") == 0) {
    scene->lfo2.curve = curve_create(CURVE_SINE);
  } else {
    ESP_LOGE(TAG, "Unknown curve. Use: linear, exp, log, s_curve, quad, sqrt, sine");
    return 1;
  }

  ESP_LOGI(TAG, "LFO2 curve: %s", curve);
  return 0;
}

// Command: lfo2_polarity - Set LFO2 polarity
static struct {
  struct arg_str *polarity;
  struct arg_end *end;
} lfo2_polarity_args;

static int cmd_lfo2_polarity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_polarity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_polarity_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* pol = lfo2_polarity_args.polarity->sval[0];

  if (strcmp(pol, "unipolar") == 0) {
    scene->lfo2.polarity = POLARITY_UNIPOLAR;
  } else if (strcmp(pol, "bipolar") == 0) {
    scene->lfo2.polarity = POLARITY_BIPOLAR;
  } else if (strcmp(pol, "inverted") == 0) {
    scene->lfo2.polarity = POLARITY_INVERTED;
  } else {
    ESP_LOGE(TAG, "Unknown polarity. Use: unipolar, bipolar, inverted");
    return 1;
  }

  ESP_LOGI(TAG, "LFO2 polarity: %s", pol);
  return 0;
}

// Command: lfo2_output - Set LFO2 output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} lfo2_output_args;

static int cmd_lfo2_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_output_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* type = lfo2_output_args.output_type->sval[0];

  if (strcmp(type, "cc") == 0) {
    scene->lfo2.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->lfo2.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type. Use: cc, note");
    return 1;
  }

  ESP_LOGI(TAG, "LFO2 output: %s", type);
  return 0;
}

// Command: lfo2_base_note - Set LFO2 base note
static struct {
  struct arg_int *note;
  struct arg_end *end;
} lfo2_base_note_args;

static int cmd_lfo2_base_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_base_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_base_note_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int note = lfo2_base_note_args.note->ival[0];
  if (note < 0 || note > 127) {
    ESP_LOGE(TAG, "Note must be 0-127");
    return 1;
  }

  scene->lfo2.base_note = (uint8_t)note;
  ESP_LOGI(TAG, "LFO2 base note: %d", note);
  return 0;
}

// Command: lfo2_note_range - Set LFO2 note range
static struct {
  struct arg_int *range;
  struct arg_end *end;
} lfo2_note_range_args;

static int cmd_lfo2_note_range(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_note_range_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_note_range_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int range = lfo2_note_range_args.range->ival[0];
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Range must be 1-127");
    return 1;
  }

  scene->lfo2.note_range = (uint8_t)range;
  ESP_LOGI(TAG, "LFO2 note range: %d semitones", range);
  return 0;
}

// Command: lfo2_velocity - Set LFO2 note velocity
static struct {
  struct arg_int *velocity;
  struct arg_end *end;
} lfo2_velocity_args;

static int cmd_lfo2_velocity(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_velocity_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_velocity_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  int vel = lfo2_velocity_args.velocity->ival[0];
  if (vel < 0 || vel > 127) {
    ESP_LOGE(TAG, "Velocity must be 0-127");
    return 1;
  }

  scene->lfo2.velocity = (uint8_t)vel;
  ESP_LOGI(TAG, "LFO2 velocity: %d", vel);
  return 0;
}

// Command: lfo2_velocity_mode - Set LFO2 velocity mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} lfo2_velocity_mode_args;

static int cmd_lfo2_velocity_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_velocity_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_velocity_mode_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* mode = lfo2_velocity_mode_args.mode->sval[0];

  if (strcmp(mode, "fixed") == 0) {
    scene->lfo2_velocity_mode = VELOCITY_MODE_FIXED;
  } else if (strcmp(mode, "gate_voltage") == 0 || strcmp(mode, "gate") == 0) {
    scene->lfo2_velocity_mode = VELOCITY_MODE_GATE_VOLTAGE;
  } else if (strcmp(mode, "touchwheel") == 0 || strcmp(mode, "tw") == 0) {
    scene->lfo2_velocity_mode = VELOCITY_MODE_TOUCHWHEEL;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: fixed, gate_voltage, touchwheel");
    return 1;
  }

  ESP_LOGI(TAG, "LFO2 velocity mode: %s", mode);
  return 0;
}

// Command: lfo2_repeat - Set LFO2 repeat mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} lfo2_repeat_args;

static int cmd_lfo2_repeat(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_repeat_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_repeat_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* mode = lfo2_repeat_args.mode->sval[0];
  bool repeat = (strcmp(mode, "loop") == 0 || strcmp(mode, "on") == 0 ||
                 strcmp(mode, "1") == 0 || strcmp(mode, "true") == 0);

  scene->lfo2_config.repeat = repeat;
  lfo_set_repeat(1, repeat);  // Also update running LFO

  ESP_LOGI(TAG, "LFO2 repeat: %s", repeat ? "loop" : "one-shot");
  return 0;
}

// Command: lfo2_trigger - Set LFO2 trigger timing
static struct {
  struct arg_str *timing;
  struct arg_end *end;
} lfo2_trigger_args;

static int cmd_lfo2_trigger(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &lfo2_trigger_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, lfo2_trigger_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  lfo_trigger_timing_t timing = lfo_trigger_timing_from_string(lfo2_trigger_args.timing->sval[0]);

  scene->lfo2_config.trigger_timing = timing;
  lfo_set_trigger_timing(1, timing);  // Also update running LFO

  ESP_LOGI(TAG, "LFO2 trigger timing: %s", lfo_trigger_timing_to_string(timing));
  return 0;
}

// Command: touchwheel_mode - Set touchwheel mode
static struct {
  struct arg_str *mode;
  struct arg_end *end;
} touchwheel_mode_args;

static int cmd_touchwheel_mode(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_mode_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_mode_args.end, argv[0]);
    return 1;
  }
  
  const char* mode_str = touchwheel_mode_args.mode->sval[0];
  touchwheel_mode_t mode;
  touchwheel_style_t default_style = TOUCHWHEEL_STYLE_ODOMETER;
  
  if (strcmp(mode_str, "pads") == 0) {
    mode = TOUCHWHEEL_MODE_PADS;
  } else if (strcmp(mode_str, "program_change") == 0 || strcmp(mode_str, "pc") == 0) {
    mode = TOUCHWHEEL_MODE_PROGRAM_CHANGE;
  } else if (strcmp(mode_str, "continuous") == 0 || strcmp(mode_str, "cc") == 0) {
    mode = TOUCHWHEEL_MODE_CONTINUOUS;
  } else if (strcmp(mode_str, "set_tempo") == 0 || strcmp(mode_str, "tempo") == 0) {
    mode = TOUCHWHEEL_MODE_SET_TEMPO;
    default_style = TOUCHWHEEL_STYLE_ENDLESS;  // Tempo defaults to endless
  } else if (strcmp(mode_str, "pitch_bend") == 0 || strcmp(mode_str, "pb") == 0) {
    mode = TOUCHWHEEL_MODE_PITCH_BEND;
    default_style = TOUCHWHEEL_STYLE_BIPOLAR;  // Pitch bend is always bipolar
  } else if (strcmp(mode_str, "aftertouch") == 0 || strcmp(mode_str, "at") == 0) {
    mode = TOUCHWHEEL_MODE_AFTERTOUCH;
  } else if (strcmp(mode_str, "double_cc") == 0 || strcmp(mode_str, "14bit") == 0) {
    mode = TOUCHWHEEL_MODE_DOUBLE_CC;
  } else {
    ESP_LOGE(TAG, "Unknown mode. Use: pads, pc, cc, tempo, pb, at, double_cc");
    return 1;
  }
  
  uint8_t scene_index = scene_get_current_index();
  scene_t* scene = scene_get_current();
  
  // Set default style for the mode
  if (scene) {
    scene->touchwheel_style = default_style;
  }
  
  esp_err_t ret = scene_set_touchwheel_mode(scene_index, mode);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set touchwheel mode");
    return 1;
  }
  
  return 0;
}

// Command: touchwheel_style - Set touchwheel continuous style (odometer or endless)
static struct {
  struct arg_str *style;
  struct arg_end *end;
} touchwheel_style_args;

static int cmd_touchwheel_style(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_style_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_style_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  // Check for mode-locked styles
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_PITCH_BEND) {
    ESP_LOGE(TAG, "Pitch bend mode requires bipolar style (cannot change)");
    return 1;
  }
  
  const char* style_str = touchwheel_style_args.style->sval[0];
  touchwheel_style_t style;
  
  if (strcmp(style_str, "odometer") == 0) {
    style = TOUCHWHEEL_STYLE_ODOMETER;
  } else if (strcmp(style_str, "endless") == 0) {
    style = TOUCHWHEEL_STYLE_ENDLESS;
  } else if (strcmp(style_str, "bipolar") == 0) {
    // Bipolar only valid for pitch_bend mode
    ESP_LOGE(TAG, "Bipolar style is only available in pitch_bend mode");
    return 1;
  } else {
    ESP_LOGE(TAG, "Unknown style. Use: odometer or endless");
    return 1;
  }
  
  scene->touchwheel_style = style;
  
  // Re-setup touchwheel if in a mode that uses style
  touchwheel_mode_t mode = scene->touchwheel_mode;
  if (mode == TOUCHWHEEL_MODE_CONTINUOUS || mode == TOUCHWHEEL_MODE_SET_TEMPO ||
      mode == TOUCHWHEEL_MODE_AFTERTOUCH || mode == TOUCHWHEEL_MODE_DOUBLE_CC) {
    scene_set_touchwheel_mode(scene_get_current_index(), mode);
  }
  
  ESP_LOGI(TAG, "Touchwheel style: %s", style_str);
  return 0;
}

// Command: touchwheel_enable - Enable/disable touchwheel continuous output
static struct {
  struct arg_str *state;
  struct arg_end *end;
} touchwheel_enable_args;

static int cmd_touchwheel_enable(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_enable_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_enable_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* state_str = touchwheel_enable_args.state->sval[0];
  bool enable = (strcmp(state_str, "on") == 0 || strcmp(state_str, "1") == 0);

  // Clean up any active notes before disabling
  if (!enable) {
    scene_touchwheel_cleanup_notes();
  }

  scene->touchwheel.enabled = enable;

  // If enabling and range is invalid, set defaults
  if (enable && scene->touchwheel.max_value == 0) {
    scene->touchwheel.max_value = 127;
  }

  ESP_LOGI(TAG, "Touchwheel: %s", enable ? "enabled" : "disabled");
  return 0;
}

// Command: touchwheel_output - Set touchwheel output type
static struct {
  struct arg_str *output_type;
  struct arg_end *end;
} touchwheel_output_args;

static int cmd_touchwheel_output(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_output_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_output_args.end, argv[0]);
    return 1;
  }

  scene_t* scene = scene_get_current();
  if (!scene) return 1;

  const char* type = touchwheel_output_args.output_type->sval[0];

  // Clean up any active notes before changing output type
  scene_touchwheel_cleanup_notes();

  if (strcmp(type, "cc") == 0) {
    scene->touchwheel.output_type = OUTPUT_TYPE_CC;
  } else if (strcmp(type, "note") == 0) {
    scene->touchwheel.output_type = OUTPUT_TYPE_NOTE;
  } else {
    ESP_LOGE(TAG, "Unknown output type (use: cc, note)");
    return 1;
  }

  ESP_LOGI(TAG, "Touchwheel output type: %s", type);
  return 0;
}

// Command: touchwheel_cc - Set touchwheel CC number(s)
static struct {
  struct arg_int *cc_nums;
  struct arg_end *end;
} touchwheel_cc_args;

static int cmd_touchwheel_cc(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_cc_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_cc_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int count = touchwheel_cc_args.cc_nums->count;
  if (count < 1) {
    ESP_LOGE(TAG, "At least one CC number required");
    return 1;
  }
  
  // Validate all CC numbers
  for (int i = 0; i < count; i++) {
    int cc = touchwheel_cc_args.cc_nums->ival[i];
    if (cc < 0 || cc > 127) {
      ESP_LOGE(TAG, "CC must be 0-127");
      return 1;
    }
  }
  
  if (count == 1) {
    // Single CC mode (backward compatible)
    scene->touchwheel.cc_number = touchwheel_cc_args.cc_nums->ival[0];
    scene->touchwheel.num_cc_numbers = 0;
    ESP_LOGI(TAG, "Touchwheel CC: %d", scene->touchwheel.cc_number);
  } else {
    // Multi-CC mode
    scene->touchwheel.num_cc_numbers = (count > MAX_MULTI_CC) ? MAX_MULTI_CC : count;
    for (int i = 0; i < scene->touchwheel.num_cc_numbers; i++) {
      scene->touchwheel.cc_numbers[i] = touchwheel_cc_args.cc_nums->ival[i];
    }
    ESP_LOGI(TAG, "Touchwheel CCs: %d CCs assigned", scene->touchwheel.num_cc_numbers);
    for (int i = 0; i < scene->touchwheel.num_cc_numbers; i++) {
      ESP_LOGI(TAG, "  CC%d", scene->touchwheel.cc_numbers[i]);
    }
  }
  
  return 0;
}

// Command: touchwheel_note - Set touchwheel note parameters
static struct {
  struct arg_int *base_note;
  struct arg_int *range;
  struct arg_int *velocity;
  struct arg_end *end;
} touchwheel_note_args;

static int cmd_touchwheel_note(int argc, char **argv) {
  int nerrors = arg_parse(argc, argv, (void **) &touchwheel_note_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, touchwheel_note_args.end, argv[0]);
    return 1;
  }
  
  scene_t* scene = scene_get_current();
  if (!scene) return 1;
  
  int base = touchwheel_note_args.base_note->ival[0];
  int range = touchwheel_note_args.range->ival[0];
  
  if (base < 0 || base > 127) {
    ESP_LOGE(TAG, "Base note must be 0-127");
    return 1;
  }
  if (range < 1 || range > 127) {
    ESP_LOGE(TAG, "Range must be 1-127 semitones");
    return 1;
  }
  
  scene->touchwheel.base_note = base;
  scene->touchwheel.note_range = range;
  
  // Optional velocity
  if (touchwheel_note_args.velocity->count > 0) {
    int vel = touchwheel_note_args.velocity->ival[0];
    if (vel < 0 || vel > 127) {
      ESP_LOGE(TAG, "Velocity must be 0-127");
      return 1;
    }
    scene->touchwheel.velocity = vel;
  }
  
  ESP_LOGI(TAG, "Touchwheel note: base=%d, range=%d, velocity=%d", 
           scene->touchwheel.base_note, scene->touchwheel.note_range, scene->touchwheel.velocity);
  return 0;
}

// Command: active - Show/set scene active status
static struct {
  struct arg_str *on_off;
  struct arg_end *end;
} active_args;

static int cmd_active(int argc, char **argv) {
  uint8_t scene_index = scene_get_current_index();
  
  // No args: show current status
  if (argc == 1) {
    bool active = scene_is_active(scene_index);
    ESP_LOGI(TAG, "Scene %d active: %s", scene_index + 1, active ? "yes" : "no");
    return 0;
  }
  
  int nerrors = arg_parse(argc, argv, (void **) &active_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, active_args.end, argv[0]);
    return 1;
  }
  
  const char* val = active_args.on_off->sval[0];
  bool new_active;
  if (strcmp(val, "on") == 0 || strcmp(val, "1") == 0 || strcmp(val, "true") == 0) {
    new_active = true;
  } else if (strcmp(val, "off") == 0 || strcmp(val, "0") == 0 || strcmp(val, "false") == 0) {
    new_active = false;
  } else {
    ESP_LOGE(TAG, "Invalid value: %s (use on/off)", val);
    return 1;
  }
  
  esp_err_t ret = scene_set_active(scene_index, new_active);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    return 1;
  }
  return 0;
}

esp_err_t scene_console_init(void) {
  ESP_LOGI(TAG, "Registering scene commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show current scene information",
    .hint = NULL,
    .func = &cmd_console_scene_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // next command
  const esp_console_cmd_t next_cmd = {
    .command = "next",
    .help = "Switch to next scene",
    .hint = NULL,
    .func = &cmd_next,
  };
  esp_console_cmd_register(&next_cmd);
  
  // prev command
  const esp_console_cmd_t prev_cmd = {
    .command = "prev",
    .help = "Switch to previous scene",
    .hint = NULL,
    .func = &cmd_prev,
  };
  esp_console_cmd_register(&prev_cmd);
  
  // goto command
  goto_args.scene_num = arg_int1(NULL, NULL, "<1-128>", "Scene number");
  goto_args.end = arg_end(2);
  
  const esp_console_cmd_t goto_cmd = {
    .command = "goto",
    .help = "Jump to specific scene",
    .hint = NULL,
    .func = &cmd_goto,
    .argtable = &goto_args
  };
  esp_console_cmd_register(&goto_cmd);
  
  // name command
  name_args.generate = arg_lit0("g", "generate", "Generate random name");
  name_args.scene_name = arg_str0(NULL, NULL, "<name>", "Scene name");
  name_args.end = arg_end(3);

  const esp_console_cmd_t name_cmd = {
    .command = "name",
    .help = "Set or generate scene name",
    .hint = NULL,
    .func = &cmd_name,
    .argtable = &name_args
  };
  esp_console_cmd_register(&name_cmd);

  // device command
  device_args.slug = arg_str0(NULL, NULL, "<slug>", "Device slug (e.g., chase_bliss.mood_mkii@0)");
  device_args.clear = arg_lit0(NULL, "clear", "Clear scene device (use global)");
  device_args.end = arg_end(3);

  const esp_console_cmd_t device_cmd = {
    .command = "device",
    .help = "Show/set device for current scene",
    .hint = NULL,
    .func = &cmd_device,
    .argtable = &device_args
  };
  esp_console_cmd_register(&device_cmd);

  // confirm command
  const esp_console_cmd_t confirm_cmd = {
    .command = "confirm",
    .help = "Confirm pending scene change",
    .hint = NULL,
    .func = &cmd_confirm,
  };
  esp_console_cmd_register(&confirm_cmd);
  
  // cancel command
  const esp_console_cmd_t cancel_cmd = {
    .command = "cancel",
    .help = "Cancel pending scene change",
    .hint = NULL,
    .func = &cmd_cancel,
  };
  esp_console_cmd_register(&cancel_cmd);
  
  // Note: channel command moved to midi context
  
  // pad command (flexible action assignment)
  pad_args.pad_num = arg_int1(NULL, NULL, "<pad>", "Pad number (0-11)");
  pad_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  pad_args.params = arg_strn(NULL, NULL, "<args>", 0, 20, "Action params (use / for multi-CC)");
  pad_args.end = arg_end(24);
  
  const esp_console_cmd_t pad_cmd = {
    .command = "pad",
    .help = "Assign action to touchpad (type 'actions' for list)",
    .hint = NULL,
    .func = &cmd_pad,
    .argtable = &pad_args
  };
  esp_console_cmd_register(&pad_cmd);
  
  // button command
  button_args.button_name = arg_str1(NULL, NULL, "<left|right|both>", "Button");
  button_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  button_args.params = arg_strn(NULL, NULL, "<args>", 0, 20, "Action params (use / for multi-CC)");
  button_args.end = arg_end(24);
  
  const esp_console_cmd_t button_cmd = {
    .command = "button",
    .help = "Assign action to button (type 'actions' for list)",
    .hint = NULL,
    .func = &cmd_button,
    .argtable = &button_args
  };
  esp_console_cmd_register(&button_cmd);
  
  // bump command
  bump_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  bump_args.params = arg_strn(NULL, NULL, "<args>", 0, 20, "Action params (use / for multi-CC)");
  bump_args.end = arg_end(24);
  
  const esp_console_cmd_t bump_cmd = {
    .command = "bump",
    .help = "Assign action to bump sensor (type 'actions' for list, 'none' to clear)",
    .hint = NULL,
    .func = &cmd_bump,
    .argtable = &bump_args
  };
  esp_console_cmd_register(&bump_cmd);
  
  // expr_switch command
  expr_switch_args.action_type = arg_str1(NULL, NULL, "<action>", "Action type");
  expr_switch_args.params = arg_strn(NULL, NULL, "<args>", 0, 20, "Action params (use / for multi-CC)");
  expr_switch_args.end = arg_end(24);
  
  const esp_console_cmd_t expr_switch_cmd = {
    .command = "expr_switch",
    .help = "Assign action to expr switch mode (type 'actions' for list, 'none' to clear)",
    .hint = NULL,
    .func = &cmd_expr_switch,
    .argtable = &expr_switch_args
  };
  esp_console_cmd_register(&expr_switch_cmd);
  
  // on_load command
  on_load_args.subcommand = arg_str1(NULL, NULL, "<cmd>", "show|clear|action_type");
  on_load_args.params = arg_strn(NULL, NULL, "<args>", 0, 20, "Action params (use / for multi-CC)");
  on_load_args.end = arg_end(24);
  
  const esp_console_cmd_t on_load_cmd = {
    .command = "on_load",
    .help = "Manage on_load actions (show|clear|add action)",
    .hint = NULL,
    .func = &cmd_on_load,
    .argtable = &on_load_args
  };
  esp_console_cmd_register(&on_load_cmd);
  
  // actions command
  const esp_console_cmd_t actions_cmd = {
    .command = "actions",
    .help = "List available action types for pad/button/bump",
    .hint = NULL,
    .func = &cmd_actions,
  };
  esp_console_cmd_register(&actions_cmd);
  
  // pc command
  pc_args.program_num = arg_int1(NULL, NULL, "<0-127>", "Program number");
  pc_args.end = arg_end(2);
  
  const esp_console_cmd_t pc_cmd = {
    .command = "pc",
    .help = "Set program number for current scene",
    .hint = NULL,
    .func = &cmd_pc,
    .argtable = &pc_args
  };
  esp_console_cmd_register(&pc_cmd);
  
  // note_channel command
  note_channel_args.channel_num = arg_int0(NULL, NULL, "<0-16>", "Note MIDI channel (0=scene channel)");
  note_channel_args.end = arg_end(2);
  
  const esp_console_cmd_t note_channel_cmd = {
    .command = "note_channel",
    .help = "Show/set note output MIDI channel (0=scene channel, 1-16=override)",
    .hint = NULL,
    .func = &cmd_note_channel,
    .argtable = &note_channel_args
  };
  esp_console_cmd_register(&note_channel_cmd);
  
  // expr_cc command
  expr_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  expr_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t expr_cc_cmd = {
    .command = "expr_cc",
    .help = "Set expression CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_expr_cc,
    .argtable = &expr_cc_args
  };
  esp_console_cmd_register(&expr_cc_cmd);
  
  // expr_curve command
  expr_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  expr_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_curve_cmd = {
    .command = "expr_curve",
    .help = "Set expression curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_expr_curve,
    .argtable = &expr_curve_args
  };
  esp_console_cmd_register(&expr_curve_cmd);
  
  // expr_polarity command
  expr_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  expr_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_polarity_cmd = {
    .command = "expr_polarity",
    .help = "Set expression polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_expr_polarity,
    .argtable = &expr_polarity_args
  };
  esp_console_cmd_register(&expr_polarity_cmd);
  
  // expr_enable command
  expr_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  expr_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_enable_cmd = {
    .command = "expr_enable",
    .help = "Enable/disable expression pedal routing",
    .hint = NULL,
    .func = &cmd_expr_enable,
    .argtable = &expr_enable_args
  };
  esp_console_cmd_register(&expr_enable_cmd);
  
  // expr_output command
  expr_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  expr_output_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_output_cmd = {
    .command = "expr_output",
    .help = "Set expression output type (cc or note)",
    .hint = NULL,
    .func = &cmd_expr_output,
    .argtable = &expr_output_args
  };
  esp_console_cmd_register(&expr_output_cmd);
  
  // expr_base_note command
  expr_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  expr_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_base_note_cmd = {
    .command = "expr_base_note",
    .help = "Set expression base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_expr_base_note,
    .argtable = &expr_base_note_args
  };
  esp_console_cmd_register(&expr_base_note_cmd);
  
  // expr_note_range command
  expr_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  expr_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_note_range_cmd = {
    .command = "expr_note_range",
    .help = "Set expression note range in semitones",
    .hint = NULL,
    .func = &cmd_expr_note_range,
    .argtable = &expr_note_range_args
  };
  esp_console_cmd_register(&expr_note_range_cmd);
  
  // expr_velocity command
  expr_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  expr_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_velocity_cmd = {
    .command = "expr_velocity",
    .help = "Set expression note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_expr_velocity,
    .argtable = &expr_velocity_args
  };
  esp_console_cmd_register(&expr_velocity_cmd);
  
  // expr_mode command
  expr_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Expression jack mode");
  expr_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t expr_mode_cmd = {
    .command = "expr_mode",
    .help = "Set expression jack mode (none/expression/sustain/sostenuto/gate/switch)",
    .hint = NULL,
    .func = &cmd_expr_mode,
    .argtable = &expr_mode_args
  };
  esp_console_cmd_register(&expr_mode_cmd);
  
  // cv_cc command
  cv_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  cv_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t cv_cc_cmd = {
    .command = "cv_cc",
    .help = "Set CV CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_cv_cc,
    .argtable = &cv_cc_args
  };
  esp_console_cmd_register(&cv_cc_cmd);
  
  // cv_curve command
  cv_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  cv_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_curve_cmd = {
    .command = "cv_curve",
    .help = "Set CV curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_cv_curve,
    .argtable = &cv_curve_args
  };
  esp_console_cmd_register(&cv_curve_cmd);
  
  // cv_polarity command
  cv_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  cv_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_polarity_cmd = {
    .command = "cv_polarity",
    .help = "Set CV polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_cv_polarity,
    .argtable = &cv_polarity_args
  };
  esp_console_cmd_register(&cv_polarity_cmd);
  
  // cv_enable command
  cv_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  cv_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_enable_cmd = {
    .command = "cv_enable",
    .help = "Enable/disable CV input routing",
    .hint = NULL,
    .func = &cmd_cv_enable,
    .argtable = &cv_enable_args
  };
  esp_console_cmd_register(&cv_enable_cmd);
  
  // cv_output command
  cv_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  cv_output_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_output_cmd = {
    .command = "cv_output",
    .help = "Set CV output type (cc or note)",
    .hint = NULL,
    .func = &cmd_cv_output,
    .argtable = &cv_output_args
  };
  esp_console_cmd_register(&cv_output_cmd);
  
  // cv_base_note command
  cv_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  cv_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_base_note_cmd = {
    .command = "cv_base_note",
    .help = "Set CV base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_cv_base_note,
    .argtable = &cv_base_note_args
  };
  esp_console_cmd_register(&cv_base_note_cmd);
  
  // cv_note_range command
  cv_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  cv_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_note_range_cmd = {
    .command = "cv_note_range",
    .help = "Set CV note range in semitones",
    .hint = NULL,
    .func = &cmd_cv_note_range,
    .argtable = &cv_note_range_args
  };
  esp_console_cmd_register(&cv_note_range_cmd);
  
  // cv_velocity command
  cv_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  cv_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_velocity_cmd = {
    .command = "cv_velocity",
    .help = "Set CV note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_cv_velocity,
    .argtable = &cv_velocity_args
  };
  esp_console_cmd_register(&cv_velocity_cmd);
  
  // cv_input_mode command
  cv_input_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "CV input mode");
  cv_input_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_input_mode_cmd = {
    .command = "cv_input_mode",
    .help = "Set CV input mode (none/cv/audio/note). Use clock_source for sync mode",
    .hint = NULL,
    .func = &cmd_cv_input_mode,
    .argtable = &cv_input_mode_args
  };
  esp_console_cmd_register(&cv_input_mode_cmd);
  
  // cv_velocity_mode command
  cv_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  cv_velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_velocity_mode_cmd = {
    .command = "cv_velocity_mode",
    .help = "Set CV NOTE input mode velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_cv_velocity_mode,
    .argtable = &cv_velocity_mode_args
  };
  esp_console_cmd_register(&cv_velocity_mode_cmd);
  
  // cv_note_velocity command (for NOTE input mode fixed velocity)
  cv_note_velocity_args.velocity = arg_int1(NULL, NULL, "<1-127>", "Velocity value");
  cv_note_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t cv_note_velocity_cmd = {
    .command = "cv_note_velocity",
    .help = "Set CV NOTE input mode fixed velocity value",
    .hint = NULL,
    .func = &cmd_cv_note_velocity,
    .argtable = &cv_note_velocity_args
  };
  esp_console_cmd_register(&cv_note_velocity_cmd);
  
  // expression_velocity_mode command
  expression_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  expression_velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t expression_velocity_mode_cmd = {
    .command = "expression_velocity_mode",
    .help = "Set expression note output velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_expression_velocity_mode,
    .argtable = &expression_velocity_mode_args
  };
  esp_console_cmd_register(&expression_velocity_mode_cmd);
  
  // proximity_velocity_mode command
  proximity_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  proximity_velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_velocity_mode_cmd = {
    .command = "proximity_velocity_mode",
    .help = "Set proximity note output velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_proximity_velocity_mode,
    .argtable = &proximity_velocity_mode_args
  };
  esp_console_cmd_register(&proximity_velocity_mode_cmd);
  
  // als_velocity_mode command
  als_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  als_velocity_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t als_velocity_mode_cmd = {
    .command = "als_velocity_mode",
    .help = "Set ALS note output velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_als_velocity_mode,
    .argtable = &als_velocity_mode_args
  };
  esp_console_cmd_register(&als_velocity_mode_cmd);
  
  // clock_source command
  clock_source_args.source = arg_str1(NULL, NULL, "<source>", "Clock source");
  clock_source_args.end = arg_end(2);
  
  const esp_console_cmd_t clock_source_cmd = {
    .command = "clock_source",
    .help = "Set tempo clock source (internal/midi/sync)",
    .hint = NULL,
    .func = &cmd_clock_source,
    .argtable = &clock_source_args
  };
  esp_console_cmd_register(&clock_source_cmd);
  
  // beat_divider command
  beat_divider_args.divider = arg_str1(NULL, NULL, "<divider>", "Beat divider");
  beat_divider_args.end = arg_end(2);
  
  // bpm command
  scene_bpm_args.bpm = arg_int1(NULL, NULL, "<bpm>", "Tempo (20-300)");
  scene_bpm_args.end = arg_end(2);
  
  const esp_console_cmd_t scene_bpm_cmd = {
    .command = "bpm",
    .help = "Set scene tempo (20-300 BPM, programming mode only)",
    .hint = NULL,
    .func = &cmd_scene_bpm,
    .argtable = &scene_bpm_args
  };
  esp_console_cmd_register(&scene_bpm_cmd);
  
  // beat_divider command
  beat_divider_args.divider = arg_str1(NULL, NULL, "<divider>", "Beat divider");
  beat_divider_args.end = arg_end(2);
  
  const esp_console_cmd_t beat_divider_cmd = {
    .command = "beat_divider",
    .help = "Set beat division (quarter/eighth/sixteenth)",
    .hint = NULL,
    .func = &cmd_beat_divider,
    .argtable = &beat_divider_args
  };
  esp_console_cmd_register(&beat_divider_cmd);

  // time_sig command
  time_sig_args.numerator = arg_int1(NULL, NULL, "<num>", "Numerator (1-16)");
  time_sig_args.denominator = arg_int1(NULL, NULL, "<denom>", "Denominator (1-16)");
  time_sig_args.end = arg_end(3);
  
  const esp_console_cmd_t time_sig_cmd = {
    .command = "time_sig",
    .help = "Set time signature (num denom)",
    .hint = NULL,
    .func = &cmd_time_sig,
    .argtable = &time_sig_args
  };
  esp_console_cmd_register(&time_sig_cmd);
  
  // use_transport command
  use_transport_args.on_off = arg_str1(NULL, NULL, "<on|off>", "Enable/disable transport controls");
  use_transport_args.end = arg_end(2);
  
  const esp_console_cmd_t use_transport_cmd = {
    .command = "use_transport",
    .help = "Control animation behavior (on: follows transport, off: always runs)",
    .hint = NULL,
    .func = &cmd_use_transport,
    .argtable = &use_transport_args
  };
  esp_console_cmd_register(&use_transport_cmd);
  
  // send_clock command
  send_clock_args.on_off = arg_str1(NULL, NULL, "<on|off>", "Enable/disable MIDI clock sending");
  send_clock_args.end = arg_end(2);
  
  const esp_console_cmd_t send_clock_cmd = {
    .command = "send_clock",
    .help = "Control whether scene sends MIDI clock (on: send, off: don't send)",
    .hint = NULL,
    .func = &cmd_send_clock,
    .argtable = &send_clock_args
  };
  esp_console_cmd_register(&send_clock_cmd);
  
  // proximity_cc command
  proximity_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  proximity_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t proximity_cc_cmd = {
    .command = "proximity_cc",
    .help = "Set proximity CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_proximity_cc,
    .argtable = &proximity_cc_args
  };
  esp_console_cmd_register(&proximity_cc_cmd);
  
  // proximity_curve command
  proximity_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  proximity_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_curve_cmd = {
    .command = "proximity_curve",
    .help = "Set proximity curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_proximity_curve,
    .argtable = &proximity_curve_args
  };
  esp_console_cmd_register(&proximity_curve_cmd);
  
  // proximity_polarity command
  proximity_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  proximity_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_polarity_cmd = {
    .command = "proximity_polarity",
    .help = "Set proximity polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_proximity_polarity,
    .argtable = &proximity_polarity_args
  };
  esp_console_cmd_register(&proximity_polarity_cmd);
  
  // proximity_enable command
  proximity_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  proximity_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_enable_cmd = {
    .command = "proximity_enable",
    .help = "Enable/disable proximity sensor routing",
    .hint = NULL,
    .func = &cmd_proximity_enable,
    .argtable = &proximity_enable_args
  };
  esp_console_cmd_register(&proximity_enable_cmd);
  
  // proximity_output command
  proximity_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  proximity_output_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_output_cmd = {
    .command = "proximity_output",
    .help = "Set proximity output type (cc or note)",
    .hint = NULL,
    .func = &cmd_proximity_output,
    .argtable = &proximity_output_args
  };
  esp_console_cmd_register(&proximity_output_cmd);
  
  // proximity_base_note command
  proximity_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  proximity_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_base_note_cmd = {
    .command = "proximity_base_note",
    .help = "Set proximity base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_proximity_base_note,
    .argtable = &proximity_base_note_args
  };
  esp_console_cmd_register(&proximity_base_note_cmd);
  
  // proximity_note_range command
  proximity_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  proximity_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_note_range_cmd = {
    .command = "proximity_note_range",
    .help = "Set proximity note range in semitones",
    .hint = NULL,
    .func = &cmd_proximity_note_range,
    .argtable = &proximity_note_range_args
  };
  esp_console_cmd_register(&proximity_note_range_cmd);
  
  // proximity_velocity command
  proximity_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  proximity_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t proximity_velocity_cmd = {
    .command = "proximity_velocity",
    .help = "Set proximity note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_proximity_velocity,
    .argtable = &proximity_velocity_args
  };
  esp_console_cmd_register(&proximity_velocity_cmd);
  
  // als_cc command
  als_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  als_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t als_cc_cmd = {
    .command = "als_cc",
    .help = "Set ALS CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_als_cc,
    .argtable = &als_cc_args
  };
  esp_console_cmd_register(&als_cc_cmd);
  
  // als_curve command
  als_curve_args.curve_name = arg_str1(NULL, NULL, "<curve>", "Curve type");
  als_curve_args.end = arg_end(2);
  
  const esp_console_cmd_t als_curve_cmd = {
    .command = "als_curve",
    .help = "Set ALS curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_als_curve,
    .argtable = &als_curve_args
  };
  esp_console_cmd_register(&als_curve_cmd);
  
  // als_polarity command
  als_polarity_args.polarity_name = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  als_polarity_args.end = arg_end(2);
  
  const esp_console_cmd_t als_polarity_cmd = {
    .command = "als_polarity",
    .help = "Set ALS polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_als_polarity,
    .argtable = &als_polarity_args
  };
  esp_console_cmd_register(&als_polarity_cmd);
  
  // als_enable command
  als_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  als_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t als_enable_cmd = {
    .command = "als_enable",
    .help = "Enable/disable ALS routing",
    .hint = NULL,
    .func = &cmd_als_enable,
    .argtable = &als_enable_args
  };
  esp_console_cmd_register(&als_enable_cmd);
  
  // als_output command
  als_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  als_output_args.end = arg_end(2);
  
  const esp_console_cmd_t als_output_cmd = {
    .command = "als_output",
    .help = "Set ALS output type (cc or note)",
    .hint = NULL,
    .func = &cmd_als_output,
    .argtable = &als_output_args
  };
  esp_console_cmd_register(&als_output_cmd);
  
  // als_base_note command
  als_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  als_base_note_args.end = arg_end(2);
  
  const esp_console_cmd_t als_base_note_cmd = {
    .command = "als_base_note",
    .help = "Set ALS base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_als_base_note,
    .argtable = &als_base_note_args
  };
  esp_console_cmd_register(&als_base_note_cmd);
  
  // als_note_range command
  als_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  als_note_range_args.end = arg_end(2);
  
  const esp_console_cmd_t als_note_range_cmd = {
    .command = "als_note_range",
    .help = "Set ALS note range in semitones",
    .hint = NULL,
    .func = &cmd_als_note_range,
    .argtable = &als_note_range_args
  };
  esp_console_cmd_register(&als_note_range_cmd);
  
  // als_velocity command
  als_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  als_velocity_args.end = arg_end(2);
  
  const esp_console_cmd_t als_velocity_cmd = {
    .command = "als_velocity",
    .help = "Set ALS note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_als_velocity,
    .argtable = &als_velocity_args
  };
  esp_console_cmd_register(&als_velocity_cmd);

  //=============================================================================
  // LFO1 Commands Registration
  //=============================================================================

  // lfo1_enable command
  lfo1_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  lfo1_enable_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_enable_cmd = {
    .command = "lfo1_enable",
    .help = "Enable/disable LFO1 MIDI output routing",
    .hint = NULL,
    .func = &cmd_lfo1_enable,
    .argtable = &lfo1_enable_args
  };
  esp_console_cmd_register(&lfo1_enable_cmd);

  // lfo1_cc command
  lfo1_cc_args.cc = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  lfo1_cc_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_cc_cmd = {
    .command = "lfo1_cc",
    .help = "Set LFO1 CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_lfo1_cc,
    .argtable = &lfo1_cc_args
  };
  esp_console_cmd_register(&lfo1_cc_cmd);

  // lfo1_curve command
  lfo1_curve_args.curve = arg_str1(NULL, NULL, "<curve>", "Curve type");
  lfo1_curve_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_curve_cmd = {
    .command = "lfo1_curve",
    .help = "Set LFO1 curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_lfo1_curve,
    .argtable = &lfo1_curve_args
  };
  esp_console_cmd_register(&lfo1_curve_cmd);

  // lfo1_polarity command
  lfo1_polarity_args.polarity = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  lfo1_polarity_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_polarity_cmd = {
    .command = "lfo1_polarity",
    .help = "Set LFO1 polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_lfo1_polarity,
    .argtable = &lfo1_polarity_args
  };
  esp_console_cmd_register(&lfo1_polarity_cmd);

  // lfo1_output command
  lfo1_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  lfo1_output_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_output_cmd = {
    .command = "lfo1_output",
    .help = "Set LFO1 output type (cc or note)",
    .hint = NULL,
    .func = &cmd_lfo1_output,
    .argtable = &lfo1_output_args
  };
  esp_console_cmd_register(&lfo1_output_cmd);

  // lfo1_base_note command
  lfo1_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  lfo1_base_note_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_base_note_cmd = {
    .command = "lfo1_base_note",
    .help = "Set LFO1 base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_lfo1_base_note,
    .argtable = &lfo1_base_note_args
  };
  esp_console_cmd_register(&lfo1_base_note_cmd);

  // lfo1_note_range command
  lfo1_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  lfo1_note_range_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_note_range_cmd = {
    .command = "lfo1_note_range",
    .help = "Set LFO1 note range in semitones",
    .hint = NULL,
    .func = &cmd_lfo1_note_range,
    .argtable = &lfo1_note_range_args
  };
  esp_console_cmd_register(&lfo1_note_range_cmd);

  // lfo1_velocity command
  lfo1_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  lfo1_velocity_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_velocity_cmd = {
    .command = "lfo1_velocity",
    .help = "Set LFO1 note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_lfo1_velocity,
    .argtable = &lfo1_velocity_args
  };
  esp_console_cmd_register(&lfo1_velocity_cmd);

  // lfo1_velocity_mode command
  lfo1_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  lfo1_velocity_mode_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_velocity_mode_cmd = {
    .command = "lfo1_velocity_mode",
    .help = "Set LFO1 note output velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_lfo1_velocity_mode,
    .argtable = &lfo1_velocity_mode_args
  };
  esp_console_cmd_register(&lfo1_velocity_mode_cmd);

  // lfo1_repeat command
  lfo1_repeat_args.mode = arg_str1(NULL, NULL, "<loop|one-shot>", "Repeat mode");
  lfo1_repeat_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_repeat_cmd = {
    .command = "lfo1_repeat",
    .help = "Set LFO1 repeat mode (loop or one-shot)",
    .hint = NULL,
    .func = &cmd_lfo1_repeat,
    .argtable = &lfo1_repeat_args
  };
  esp_console_cmd_register(&lfo1_repeat_cmd);

  // lfo1_trigger command
  lfo1_trigger_args.timing = arg_str1(NULL, NULL, "<immediate|beat|bar>", "Trigger timing");
  lfo1_trigger_args.end = arg_end(2);

  const esp_console_cmd_t lfo1_trigger_cmd = {
    .command = "lfo1_trigger",
    .help = "Set LFO1 start trigger timing (immediate/beat/bar)",
    .hint = NULL,
    .func = &cmd_lfo1_trigger,
    .argtable = &lfo1_trigger_args
  };
  esp_console_cmd_register(&lfo1_trigger_cmd);

  //=============================================================================
  // LFO2 Commands Registration
  //=============================================================================

  // lfo2_enable command
  lfo2_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  lfo2_enable_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_enable_cmd = {
    .command = "lfo2_enable",
    .help = "Enable/disable LFO2 MIDI output routing",
    .hint = NULL,
    .func = &cmd_lfo2_enable,
    .argtable = &lfo2_enable_args
  };
  esp_console_cmd_register(&lfo2_enable_cmd);

  // lfo2_cc command
  lfo2_cc_args.cc = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s)");
  lfo2_cc_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_cc_cmd = {
    .command = "lfo2_cc",
    .help = "Set LFO2 CC(s) - multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_lfo2_cc,
    .argtable = &lfo2_cc_args
  };
  esp_console_cmd_register(&lfo2_cc_cmd);

  // lfo2_curve command
  lfo2_curve_args.curve = arg_str1(NULL, NULL, "<curve>", "Curve type");
  lfo2_curve_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_curve_cmd = {
    .command = "lfo2_curve",
    .help = "Set LFO2 curve (linear/exp/log/s_curve/quad/sqrt/sine)",
    .hint = NULL,
    .func = &cmd_lfo2_curve,
    .argtable = &lfo2_curve_args
  };
  esp_console_cmd_register(&lfo2_curve_cmd);

  // lfo2_polarity command
  lfo2_polarity_args.polarity = arg_str1(NULL, NULL, "<polarity>", "Polarity type");
  lfo2_polarity_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_polarity_cmd = {
    .command = "lfo2_polarity",
    .help = "Set LFO2 polarity (unipolar/bipolar/inverted)",
    .hint = NULL,
    .func = &cmd_lfo2_polarity,
    .argtable = &lfo2_polarity_args
  };
  esp_console_cmd_register(&lfo2_polarity_cmd);

  // lfo2_output command
  lfo2_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  lfo2_output_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_output_cmd = {
    .command = "lfo2_output",
    .help = "Set LFO2 output type (cc or note)",
    .hint = NULL,
    .func = &cmd_lfo2_output,
    .argtable = &lfo2_output_args
  };
  esp_console_cmd_register(&lfo2_output_cmd);

  // lfo2_base_note command
  lfo2_base_note_args.note = arg_int1(NULL, NULL, "<0-127>", "Base MIDI note");
  lfo2_base_note_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_base_note_cmd = {
    .command = "lfo2_base_note",
    .help = "Set LFO2 base note for NOTE mode",
    .hint = NULL,
    .func = &cmd_lfo2_base_note,
    .argtable = &lfo2_base_note_args
  };
  esp_console_cmd_register(&lfo2_base_note_cmd);

  // lfo2_note_range command
  lfo2_note_range_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  lfo2_note_range_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_note_range_cmd = {
    .command = "lfo2_note_range",
    .help = "Set LFO2 note range in semitones",
    .hint = NULL,
    .func = &cmd_lfo2_note_range,
    .argtable = &lfo2_note_range_args
  };
  esp_console_cmd_register(&lfo2_note_range_cmd);

  // lfo2_velocity command
  lfo2_velocity_args.velocity = arg_int1(NULL, NULL, "<0-127>", "Note velocity");
  lfo2_velocity_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_velocity_cmd = {
    .command = "lfo2_velocity",
    .help = "Set LFO2 note velocity for NOTE mode",
    .hint = NULL,
    .func = &cmd_lfo2_velocity,
    .argtable = &lfo2_velocity_args
  };
  esp_console_cmd_register(&lfo2_velocity_cmd);

  // lfo2_velocity_mode command
  lfo2_velocity_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Velocity mode");
  lfo2_velocity_mode_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_velocity_mode_cmd = {
    .command = "lfo2_velocity_mode",
    .help = "Set LFO2 note output velocity mode (fixed/gate_voltage/touchwheel)",
    .hint = NULL,
    .func = &cmd_lfo2_velocity_mode,
    .argtable = &lfo2_velocity_mode_args
  };
  esp_console_cmd_register(&lfo2_velocity_mode_cmd);

  // lfo2_repeat command
  lfo2_repeat_args.mode = arg_str1(NULL, NULL, "<loop|one-shot>", "Repeat mode");
  lfo2_repeat_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_repeat_cmd = {
    .command = "lfo2_repeat",
    .help = "Set LFO2 repeat mode (loop or one-shot)",
    .hint = NULL,
    .func = &cmd_lfo2_repeat,
    .argtable = &lfo2_repeat_args
  };
  esp_console_cmd_register(&lfo2_repeat_cmd);

  // lfo2_trigger command
  lfo2_trigger_args.timing = arg_str1(NULL, NULL, "<immediate|beat|bar>", "Trigger timing");
  lfo2_trigger_args.end = arg_end(2);

  const esp_console_cmd_t lfo2_trigger_cmd = {
    .command = "lfo2_trigger",
    .help = "Set LFO2 start trigger timing (immediate/beat/bar)",
    .hint = NULL,
    .func = &cmd_lfo2_trigger,
    .argtable = &lfo2_trigger_args
  };
  esp_console_cmd_register(&lfo2_trigger_cmd);

  // touchwheel_mode command
  touchwheel_mode_args.mode = arg_str1(NULL, NULL, "<mode>", "Touchwheel mode");
  touchwheel_mode_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_mode_cmd = {
    .command = "touchwheel_mode",
    .help = "Set touchwheel mode: pads, pc, continuous/cc, tempo, pb, at, double_cc",
    .hint = NULL,
    .func = &cmd_touchwheel_mode,
    .argtable = &touchwheel_mode_args
  };
  esp_console_cmd_register(&touchwheel_mode_cmd);
  
  // touchwheel_style command
  touchwheel_style_args.style = arg_str1(NULL, NULL, "<odometer|endless>", "Touchwheel style");
  touchwheel_style_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_style_cmd = {
    .command = "touchwheel_style",
    .help = "Set touchwheel style (odometer: ~15 positions, endless: full range)",
    .hint = NULL,
    .func = &cmd_touchwheel_style,
    .argtable = &touchwheel_style_args
  };
  esp_console_cmd_register(&touchwheel_style_cmd);
  
  // touchwheel_enable command
  touchwheel_enable_args.state = arg_str1(NULL, NULL, "<on|off>", "Enable/disable");
  touchwheel_enable_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_enable_cmd = {
    .command = "touchwheel_enable",
    .help = "Enable/disable touchwheel continuous output",
    .hint = NULL,
    .func = &cmd_touchwheel_enable,
    .argtable = &touchwheel_enable_args
  };
  esp_console_cmd_register(&touchwheel_enable_cmd);
  
  // touchwheel_output command
  touchwheel_output_args.output_type = arg_str1(NULL, NULL, "<cc|note>", "Output type");
  touchwheel_output_args.end = arg_end(2);
  
  const esp_console_cmd_t touchwheel_output_cmd = {
    .command = "touchwheel_output",
    .help = "Set touchwheel output type (cc or note)",
    .hint = NULL,
    .func = &cmd_touchwheel_output,
    .argtable = &touchwheel_output_args
  };
  esp_console_cmd_register(&touchwheel_output_cmd);
  
  // touchwheel_cc command (supports multiple CCs)
  touchwheel_cc_args.cc_nums = arg_intn(NULL, NULL, "<cc>", 1, MAX_MULTI_CC, "CC number(s) (up to 4)");
  touchwheel_cc_args.end = arg_end(5);
  
  const esp_console_cmd_t touchwheel_cc_cmd = {
    .command = "touchwheel_cc",
    .help = "Set touchwheel CC number(s) - use multiple for simultaneous control",
    .hint = NULL,
    .func = &cmd_touchwheel_cc,
    .argtable = &touchwheel_cc_args
  };
  esp_console_cmd_register(&touchwheel_cc_cmd);
  
  // touchwheel_note command
  touchwheel_note_args.base_note = arg_int1(NULL, NULL, "<0-127>", "Base note");
  touchwheel_note_args.range = arg_int1(NULL, NULL, "<1-127>", "Range in semitones");
  touchwheel_note_args.velocity = arg_int0(NULL, NULL, "<0-127>", "Velocity (optional)");
  touchwheel_note_args.end = arg_end(4);
  
  const esp_console_cmd_t touchwheel_note_cmd = {
    .command = "touchwheel_note",
    .help = "Set touchwheel note parameters (base, range, [velocity])",
    .hint = NULL,
    .func = &cmd_touchwheel_note,
    .argtable = &touchwheel_note_args
  };
  esp_console_cmd_register(&touchwheel_note_cmd);
  
  // active command
  active_args.on_off = arg_str0(NULL, NULL, "<on|off>", "Activate/deactivate scene");
  active_args.end = arg_end(2);
  
  const esp_console_cmd_t active_cmd = {
    .command = "active",
    .help = "Show or set scene active status (on/off)",
    .hint = NULL,
    .func = &cmd_active,
    .argtable = &active_args
  };
  esp_console_cmd_register(&active_cmd);
  
  return ESP_OK;
}

void scene_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering scene commands");
  
  // Deregister all commands
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

