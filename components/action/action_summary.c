#include "action_summary.h"
#include "assets_manager.h"
#include "scene.h"
#include "tempo.h"
#include "touchwheel_mode_mapping.h"
#include "lfo.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// Display names for action types live in action_strings.c
// (action_type_to_string); the ACTION_NONE case is handled by the early
// return in action_format_summary, so no separate "Undefined" label is needed
// here.

void action_summary_init(action_summary_t *summary) {
  if (!summary) return;
  memset(summary, 0, sizeof(action_summary_t));
}

void action_summary_set_input(action_summary_t *summary, summary_input_t input,
    bool is_pads_mode) {
  if (!summary) return;

  switch (input) {
    case SUMMARY_INPUT_PAD_0:
    case SUMMARY_INPUT_PAD_1:
    case SUMMARY_INPUT_PAD_2:
    case SUMMARY_INPUT_PAD_3:
    case SUMMARY_INPUT_PAD_4:
    case SUMMARY_INPUT_PAD_5:
    case SUMMARY_INPUT_PAD_6:
    case SUMMARY_INPUT_PAD_7:
      if (is_pads_mode) {
        snprintf(summary->input_name, sizeof(summary->input_name),
          "Pad %d", (input - SUMMARY_INPUT_PAD_0) + 1);
      } else {
        snprintf(summary->input_name, sizeof(summary->input_name), "Touchwheel");
      }
      break;
    case SUMMARY_INPUT_PAD_8:
      snprintf(summary->input_name, sizeof(summary->input_name), "Enter");
      break;
    case SUMMARY_INPUT_PAD_9:
      snprintf(summary->input_name, sizeof(summary->input_name), "Up");
      break;
    case SUMMARY_INPUT_PAD_10:
      snprintf(summary->input_name, sizeof(summary->input_name), "Jump");
      break;
    case SUMMARY_INPUT_PAD_11:
      snprintf(summary->input_name, sizeof(summary->input_name), "Down");
      break;
    case SUMMARY_INPUT_TOUCHWHEEL:
      snprintf(summary->input_name, sizeof(summary->input_name), "Touchwheel");
      break;
    case SUMMARY_INPUT_BUTTON_L:
      snprintf(summary->input_name, sizeof(summary->input_name), "Left Button");
      break;
    case SUMMARY_INPUT_BUTTON_R:
      snprintf(summary->input_name, sizeof(summary->input_name), "Right Button");
      break;
    case SUMMARY_INPUT_BUTTON_BOTH:
      snprintf(summary->input_name, sizeof(summary->input_name), "Both Buttons");
      break;
    case SUMMARY_INPUT_BUMP:
      snprintf(summary->input_name, sizeof(summary->input_name), "Bump");
      break;
    case SUMMARY_INPUT_EXPRESSION:
      snprintf(summary->input_name, sizeof(summary->input_name), "Expression");
      break;
    case SUMMARY_INPUT_CV:
      snprintf(summary->input_name, sizeof(summary->input_name), "CV Input");
      break;
    case SUMMARY_INPUT_PROXIMITY:
      snprintf(summary->input_name, sizeof(summary->input_name), "Proximity");
      break;
    case SUMMARY_INPUT_ALS:
      snprintf(summary->input_name, sizeof(summary->input_name), "Ambient Light");
      break;
    case SUMMARY_INPUT_LFO1:
      snprintf(summary->input_name, sizeof(summary->input_name), "LFO 1");
      break;
    case SUMMARY_INPUT_LFO2:
      snprintf(summary->input_name, sizeof(summary->input_name), "LFO 2");
      break;
    case SUMMARY_INPUT_SAMPLE_HOLD:
      snprintf(summary->input_name, sizeof(summary->input_name), "S+H");
      break;
    case SUMMARY_INPUT_ON_LOAD:
      snprintf(summary->input_name, sizeof(summary->input_name), "On Load");
      break;
    case SUMMARY_INPUT_ON_PLAY:
      snprintf(summary->input_name, sizeof(summary->input_name), "On Play");
      break;
    default:
      snprintf(summary->input_name, sizeof(summary->input_name), "Unknown");
      break;
  }
}

// Get output type display name
static const char* output_type_name(output_type_t type) {
  switch (type) {
    case OUTPUT_TYPE_CC: return "Control Change";
    case OUTPUT_TYPE_NOTE: return "Notes";
    case OUTPUT_TYPE_LFO_RATE: return "LFO Rate";
    case OUTPUT_TYPE_LFO_DEPTH: return "LFO Depth";
    case OUTPUT_TYPE_LFO2_RATE: return "LFO2 Rate";
    case OUTPUT_TYPE_LFO2_DEPTH: return "LFO2 Depth";
    case OUTPUT_TYPE_LFO1_RATE: return "LFO1 Rate";
    case OUTPUT_TYPE_LFO1_DEPTH: return "LFO1 Depth";
    case OUTPUT_TYPE_RTG_RATE: return "RTG Rate";
    case OUTPUT_TYPE_SH_RATE: return "S+H Rate";
    case OUTPUT_TYPE_PITCH_BEND: return "Pitch Bend";
    case OUTPUT_TYPE_TEMPO_NUDGE: return "Tempo Nudge";
    default: return "Unknown";
  }
}

static const char *SUMMARY_NOTE_NAMES[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void summary_note_name(uint8_t midi_note, char *buf, size_t cap) {
  if (midi_note > 127) midi_note = 127;
  int octave = (midi_note / 12) - 1;
  int note_idx = midi_note % 12;
  snprintf(buf, cap, "%s%d", SUMMARY_NOTE_NAMES[note_idx], octave);
}

static const char *summary_scene_name_for_target(uint8_t target_index) {
  uint16_t count = scene_get_total_count();
  for (uint16_t i = 0; i < count; i++) {
    if (scene_get_index_by_position(i) == target_index)
      return scene_get_name_by_position(i);
  }
  return NULL;
}

static int summary_append_preset_cycle_list(char *buf, size_t cap, const action_t *action) {
  uint8_t num = action->params.preset.num_presets;
  if (num < 2) num = 2;
  if (num > 8) num = 8;

  int pos = 0;
  for (uint8_t i = 0; i < num; i++) {
    pos += snprintf(buf + pos, cap - (size_t)pos, "%s%u",
      (i == 0) ? "" : ", ", (unsigned)action->params.preset.cycle_presets[i]);
    if (pos < 0 || (size_t)pos >= cap) break;
  }
  return pos;
}

void action_format_summary(const action_t *action, action_summary_t *summary,
    uint8_t scene_index) {
  if (!summary) return;

  summary->has_param = false;
  summary->has_value = false;

  if (!action || action->type == ACTION_NONE) {
    snprintf(summary->type_name, sizeof(summary->type_name), "Undefined");
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
    return;
  }

  // Composite (variant-aware) display name -- "Tempo > Hold" rather than
  // bare "Tempo" so the summary card tells the user which operation runs.
  action_get_display_name(action, summary->type_name, sizeof(summary->type_name));

  // Control family: show the first CC slot detail. Variant determines
  // the value-side formatting (single value vs press/release vs cycle).
  if (action->type == ACTION_CONTROL && action->variant == VARIANT_SET &&
      action->params.control.num_ccs > 0) {
    uint8_t cc_num = action->params.control.cc_numbers[0];
    uint8_t cc_val = action->params.control.values[0];

    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, cc_num) : NULL;
    const char *val_name = device ? assets_get_discrete_name(device, cc_num, cc_val) : NULL;

    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
      summary->has_param = true;
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)cc_num);
      summary->has_param = true;
    }

    if (val_name) {
      snprintf(summary->param_value, sizeof(summary->param_value), "%s", val_name);
    } else {
      snprintf(summary->param_value, sizeof(summary->param_value), "%u",
        (unsigned)cc_val);
    }
    summary->has_value = true;

  } else if (action->type == ACTION_CONTROL && action->variant == VARIANT_HOLD &&
             action->params.control.num_ccs > 0) {
    uint8_t cc_num = action->params.control.cc_numbers[0];
    uint8_t val1 = action->params.control.values[0];
    uint8_t val2 = action->params.control.values2[0];

    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, cc_num) : NULL;

    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)cc_num);
    }
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u / %u",
      (unsigned)val1, (unsigned)val2);
    summary->has_value = true;

  } else if (action->type == ACTION_CONTROL && action->variant == VARIANT_CYCLE &&
             action->params.control.num_ccs > 0) {
    uint8_t cc_num = action->params.control.cc_numbers[0];
    uint8_t num_steps = action->params.control.num_cycle_steps;

    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, cc_num) : NULL;

    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)cc_num);
    }
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u values",
      (unsigned)num_steps);
    summary->has_value = true;

  } else if (action->type == ACTION_NOTE) {
    if (action->params.note.note == ACTION_NOTE_RANDOM) {
      uint8_t lo = action->params.note.random_floor;
      uint8_t hi = action->params.note.random_ceiling;
      if (lo < 36) lo = 36;
      if (hi > 96) hi = 96;
      if (lo == 36 && hi == 96) {
        snprintf(summary->param_name, sizeof(summary->param_name), "Random");
      } else {
        char lo_name[8];
        char hi_name[8];
        summary_note_name(lo, lo_name, sizeof(lo_name));
        summary_note_name(hi, hi_name, sizeof(hi_name));
        snprintf(summary->param_name, sizeof(summary->param_name), "Random %s-%s",
          lo_name, hi_name);
      }
    } else {
      summary_note_name(action->params.note.note, summary->param_name,
        sizeof(summary->param_name));
    }
    summary->has_param = true;
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

    char vel_part[16];
    if (vel_name) snprintf(vel_part, sizeof(vel_part), "%s", vel_name);
    else snprintf(vel_part, sizeof(vel_part), "Vel %u", (unsigned)action->params.note.velocity);

    char* val = summary->param_value;
    size_t val_cap = sizeof(summary->param_value);
    int pos = snprintf(val, val_cap, "%s", vel_part);
    if (voices > 1 && pos > 0 && (size_t)pos < val_cap)
      pos += snprintf(val + pos, val_cap - (size_t)pos, " x%u", (unsigned)voices);
    if (action->params.note.bass && pos > 0 && (size_t)pos < val_cap)
      pos += snprintf(val + pos, val_cap - (size_t)pos, " Bass");
    if (action->params.note.aftertouch && pos > 0 && (size_t)pos < val_cap)
      snprintf(val + pos, val_cap - (size_t)pos, " AT");
    summary->has_value = true;

  } else if (action->type == ACTION_PIANO_PEDAL) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Pedal");
    summary->has_param = true;
    const char *name;
    switch (action->params.piano_pedal.cc_number) {
      case 64: name = "Damper";    break;
      case 66: name = "Sostenuto"; break;
      case 67: name = "Soft";      break;
      case 68: name = "Legato";    break;
      case 69: name = "Hold 2";    break;
      default: name = "Damper";    break;
    }
    snprintf(summary->param_value, sizeof(summary->param_value), "%s", name);
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET && action->variant == VARIANT_SET) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Program");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u",
      (unsigned)action->params.preset.program + 1);
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET && action->variant == VARIANT_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Programs");
    summary->has_param = true;
    if (action->params.preset.release_to_original) {
      snprintf(summary->param_value, sizeof(summary->param_value), "%u / Orig",
        (unsigned)action->params.preset.press_preset + 1);
    } else {
      snprintf(summary->param_value, sizeof(summary->param_value), "%u / %u",
        (unsigned)action->params.preset.press_preset + 1,
        (unsigned)action->params.preset.release_preset + 1);
    }
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET && action->variant == VARIANT_CYCLE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Programs");
    summary->has_param = true;
    summary_append_preset_cycle_list(summary->param_value,
      sizeof(summary->param_value), action);
    summary->has_value = summary->param_value[0] != '\0';

  } else if (action->type == ACTION_SCENE && action->variant == VARIANT_SET) {
    const char *name = summary_scene_name_for_target(action->params.target.number);
    if (name) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "Scene %u",
        (unsigned)action->params.target.number + 1);
    }
    summary->has_param = true;

  } else if (action->type == ACTION_TEMPO && action->variant == VARIANT_SET) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    if (action->params.tempo.bpm == ACTION_TEMPO_BPM_ORIGINAL) {
      snprintf(summary->param_value, sizeof(summary->param_value), "Original");
    } else if (action->params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM) {
      char lo_buf[8], hi_buf[8];
      tempo_format_bpm(lo_buf, sizeof(lo_buf), action->params.tempo.random_floor);
      tempo_format_bpm(hi_buf, sizeof(hi_buf), action->params.tempo.random_ceiling);
      snprintf(summary->param_value, sizeof(summary->param_value), "Random %s-%s",
        lo_buf, hi_buf);
    } else {
      tempo_format_bpm(summary->param_value, sizeof(summary->param_value),
        action->params.tempo.bpm);
    }
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO && action->variant == VARIANT_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    char press_buf[8], release_buf[8];
    tempo_format_bpm(press_buf, sizeof(press_buf), action->params.tempo.press_bpm);
    tempo_format_bpm(release_buf, sizeof(release_buf), action->params.tempo.release_bpm);
    snprintf(summary->param_value, sizeof(summary->param_value), "%s / %s",
      press_buf, release_buf);
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO && action->variant == VARIANT_CYCLE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u values",
      (unsigned)action->params.tempo.num_tempos);
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO &&
             (action->variant == VARIANT_INCREMENT ||
              action->variant == VARIANT_DECREMENT)) {
    uint16_t amount_x10 = action->params.tempo.inc_amount;
    if (amount_x10 == 0) amount_x10 = 10;
    char amount_buf[16];
    tempo_format_bpm(amount_buf, sizeof(amount_buf), amount_x10);
    snprintf(summary->param_name, sizeof(summary->param_name), "Step");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%c%s BPM",
      action->variant == VARIANT_INCREMENT ? '+' : '-', amount_buf);
    summary->has_value = true;

  } else if (action->type == ACTION_TOUCHWHEEL && action->variant == VARIANT_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Modes");
    summary->has_param = true;
    const char* press_name = touchwheel_get_mode_name(action->params.tw_mode.mode);
    if (action->params.tw_mode.release_to_original) {
      snprintf(summary->param_value, sizeof(summary->param_value), "%s / Orig", press_name);
    } else {
      const char* release_name = touchwheel_get_mode_name(action->params.tw_mode.mode2);
      snprintf(summary->param_value, sizeof(summary->param_value), "%s / %s",
        press_name, release_name);
    }
    summary->has_value = true;

  } else if (action->type == ACTION_TOUCHWHEEL && action->variant == VARIANT_CYCLE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Modes");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u modes",
      (unsigned)action->params.tw_mode.num_modes);
    summary->has_value = true;

  } else if (action->type == ACTION_LFO) {
    uint8_t slot = action->params.lfo.slot;
    if (slot == 3) {
      snprintf(summary->param_name, sizeof(summary->param_name), "Both LFOs");
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "LFO %u",
        (unsigned)slot);
    }
    summary->has_param = true;

    // MODIFY -- list each non-sentinel override as a compact tag (Wave / Rate
    // / Floor / Ceil / Res / Steps). Tags that wouldn't fit are
    // silently dropped rather than truncating mid-tag.
    if (action->variant == VARIANT_MODIFY) {
      // Static lookup keeps the per-call code small and easy to scan.
      static const struct { uint8_t field; const char *tag; } overrides[] = {
        { 0, "Wave"     },
        { 1, "RateMode" },
        { 2, "Rate"     },
        { 3, "Div"      },
        { 4, "Floor"    },
        { 5, "Ceil"     },
        { 6, "Res"      },
        { 7, "Steps"    },
      };
      bool active[8] = {
        action->params.lfo.waveform        != ACTION_LFO_ORIG_U8,
        action->params.lfo.rate_mode       != ACTION_LFO_ORIG_U8,
        action->params.lfo.rate_hz_x100    != ACTION_LFO_ORIG_U16,
        action->params.lfo.division        != ACTION_LFO_ORIG_U8,
        action->params.lfo.floor           != ACTION_LFO_ORIG_U8,
        action->params.lfo.ceiling         != ACTION_LFO_ORIG_U8,
        action->params.lfo.resolution_mode != ACTION_LFO_ORIG_U8,
        action->params.lfo.manual_steps    != ACTION_LFO_ORIG_STEPS,
      };
      char *out = summary->param_value;
      size_t out_cap = sizeof(summary->param_value);
      size_t pos = 0;
      out[0] = '\0';
      for (size_t i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++) {
        if (!active[overrides[i].field]) continue;
        size_t n = strlen(overrides[i].tag);
        size_t need = (pos == 0) ? n : n + 1;
        if (pos + need >= out_cap) break;
        if (pos > 0) out[pos++] = '/';
        memcpy(out + pos, overrides[i].tag, n);
        pos += n;
        out[pos] = '\0';
      }
      if (pos == 0)
        snprintf(out, out_cap, "no overrides");
      summary->has_value = true;
    }

  } else if (action->type == ACTION_RANDOMIZE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "%u CCs",
      (unsigned)action->params.randomize.num_ccs);
    summary->has_param = true;

  } else if (action->type == ACTION_BOOMERANG) {
    const char *out_name = output_type_name((output_type_t)action->params.boomerang.output_type);
    if (action->params.boomerang.output_type == OUTPUT_TYPE_CC) {
      uint8_t cc_num = action->params.boomerang.cc_number;
      const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
      const char *cc_name = device ? assets_get_cc_name(device, cc_num) : NULL;
      if (cc_name && strcmp(cc_name, "Undefined") != 0) {
        snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
      } else {
        snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
          (unsigned)cc_num);
      }
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", out_name);
    }
    summary->has_param = true;

    if (action->params.boomerang.target_mode == BOOMERANG_TARGET_RANDOM) {
      snprintf(summary->param_value, sizeof(summary->param_value), "-> Random");
    } else {
      snprintf(summary->param_value, sizeof(summary->param_value), "-> %u",
        (unsigned)action->params.boomerang.target_value);
    }
    summary->has_value = true;

  } else {
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
  }
}

void continuous_format_summary(const continuous_mapping_t *mapping,
    action_summary_t *summary, uint8_t scene_index) {
  if (!summary) return;

  summary->has_param = false;
  summary->has_value = false;

  if (!mapping || !mapping->enabled) {
    snprintf(summary->type_name, sizeof(summary->type_name), "Disabled");
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
    return;
  }

  const char *type_name = output_type_name(mapping->output_type);
  snprintf(summary->type_name, sizeof(summary->type_name), "%s", type_name);

  if (mapping->output_type == OUTPUT_TYPE_CC) {
    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, mapping->cc_number) : NULL;
    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)mapping->cc_number);
    }
    summary->has_param = true;

    // Show current range
    snprintf(summary->param_value, sizeof(summary->param_value), "%u - %u",
      (unsigned)mapping->min_value, (unsigned)mapping->max_value);
    summary->has_value = true;

  } else if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Base: %u",
      (unsigned)mapping->base_note);
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "Range: %u",
      (unsigned)mapping->note_range);
    summary->has_value = true;

  } else if (mapping->output_type == OUTPUT_TYPE_LFO_RATE ||
             mapping->output_type == OUTPUT_TYPE_LFO_DEPTH) {
    switch (mapping->lfo_target) {
      case LFO_TARGET_LFO1:
        snprintf(summary->param_name, sizeof(summary->param_name), "Target: LFO 1");
        break;
      case LFO_TARGET_LFO2:
        snprintf(summary->param_name, sizeof(summary->param_name), "Target: LFO 2");
        break;
      case LFO_TARGET_BOTH:
        snprintf(summary->param_name, sizeof(summary->param_name), "Target: Both");
        break;
      default:
        snprintf(summary->param_name, sizeof(summary->param_name), "Target: Unknown");
        break;
    }
    summary->has_param = true;

  } else if (mapping->output_type == OUTPUT_TYPE_PITCH_BEND) {
    summary->has_param = false;
  }
}

void touchwheel_format_summary(const scene_t *scene, action_summary_t *summary) {
  if (!summary) return;

  summary->has_param = false;
  summary->has_value = false;

  if (!scene) {
    snprintf(summary->type_name, sizeof(summary->type_name), "No Scene");
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
    return;
  }

  // Get the user-facing mode index and name
  uint8_t mode_idx = touchwheel_get_current_mode_index((scene_t*)scene);
  const char *mode_name = touchwheel_get_mode_name(mode_idx);
  snprintf(summary->type_name, sizeof(summary->type_name), "%s", mode_name);

  // For certain modes, show additional info
  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    // Get CC info
    uint8_t scene_index = scene_get_current_index();
    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ?
      assets_get_cc_name(device, scene->touchwheel.cc_number) : NULL;

    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)scene->touchwheel.cc_number);
    }
    summary->has_param = true;
  }
}

void lfo_format_summary(uint8_t lfo_slot, const scene_t *scene,
    action_summary_t *summary, uint8_t scene_index) {
  if (!summary || lfo_slot >= LFO_NUM_SLOTS) return;

  summary->has_param = false;
  summary->has_value = false;

  const lfo_config_t *config = (lfo_slot == 0) ?
    &scene->lfo1_config : &scene->lfo2_config;
  const continuous_mapping_t *mapping = (lfo_slot == 0) ?
    &scene->lfo1 : &scene->lfo2;

  if (!config->enabled && !lfo_is_enabled(lfo_slot)) {
    snprintf(summary->type_name, sizeof(summary->type_name), "Disabled");
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
    return;
  }

  // Show waveform
  const char *waveform = lfo_waveform_to_string(config->waveform);
  snprintf(summary->type_name, sizeof(summary->type_name), "%s", waveform);

  // Show output type
  if (mapping->output_type == OUTPUT_TYPE_CC) {
    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, mapping->cc_number) : NULL;
    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)mapping->cc_number);
    }
    summary->has_param = true;
  } else if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Notes %u-%u",
      (unsigned)mapping->base_note,
      (unsigned)(mapping->base_note + mapping->note_range));
    summary->has_param = true;
  }

  // Show rate info
  if (config->rate_mode == LFO_RATE_MODE_FREE) {
    float hz = (float)config->rate_hz_x100 / 100.0f;
    snprintf(summary->param_value, sizeof(summary->param_value), "%.2f Hz", hz);
    summary->has_value = true;
  } else if (config->rate_mode == LFO_RATE_MODE_TEMPO) {
    const char *div = lfo_division_to_string(config->division);
    snprintf(summary->param_value, sizeof(summary->param_value), "%s", div);
    summary->has_value = true;
  } else {
    const char *rate_src = lfo_rate_mode_to_string(config->rate_mode);
    snprintf(summary->param_value, sizeof(summary->param_value), "%s", rate_src);
    summary->has_value = true;
  }
}

void sample_hold_format_summary(const scene_t *scene, action_summary_t *summary,
    uint8_t scene_index) {
  if (!summary) return;

  summary->has_param = false;
  summary->has_value = false;

  if (!scene || !sample_hold_is_enabled()) {
    snprintf(summary->type_name, sizeof(summary->type_name), "Disabled");
    summary->param_name[0] = '\0';
    summary->param_value[0] = '\0';
    return;
  }

  const sample_hold_config_t *config = &scene->sample_hold_config;
  const continuous_mapping_t *mapping = &scene->sample_hold;

  // Show mode
  const char *mode = sample_hold_mode_to_string(config->mode);
  snprintf(summary->type_name, sizeof(summary->type_name), "%s", mode);

  // Show output
  if (mapping->output_type == OUTPUT_TYPE_CC) {
    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    const char *cc_name = device ? assets_get_cc_name(device, mapping->cc_number) : NULL;
    if (cc_name && strcmp(cc_name, "Undefined") != 0) {
      snprintf(summary->param_name, sizeof(summary->param_name), "%s", cc_name);
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "CC %u",
        (unsigned)mapping->cc_number);
    }
    summary->has_param = true;
  }

  // Show rate
  if (config->rate_mode == SAMPLE_HOLD_RATE_MODE_FREE) {
    float hz = (float)config->rate_hz_x100 / 100.0f;
    snprintf(summary->param_value, sizeof(summary->param_value), "%.1f Hz", hz);
    summary->has_value = true;
  } else {
    float mult = (float)config->sync_mult_x1000 / 1000.0f;
    snprintf(summary->param_value, sizeof(summary->param_value), "%.2fx BPM", mult);
    summary->has_value = true;
  }
}

void action_summary_format_line(const action_summary_t *summary,
    char *buf, size_t len) {
  if (!summary || !buf || len == 0) return;
  buf[0] = '\0';

  if (summary->input_name[0] == '\0' && summary->type_name[0] == '\0') return;

  if (summary->has_param && summary->has_value) {
    if (summary->input_name[0] != '\0') {
      snprintf(buf, len, "%s: %s %s %s",
        summary->input_name, summary->type_name,
        summary->param_name, summary->param_value);
    } else {
      snprintf(buf, len, "%s %s %s",
        summary->type_name, summary->param_name, summary->param_value);
    }
  } else if (summary->has_param) {
    if (summary->input_name[0] != '\0') {
      snprintf(buf, len, "%s: %s %s",
        summary->input_name, summary->type_name, summary->param_name);
    } else {
      snprintf(buf, len, "%s %s", summary->type_name, summary->param_name);
    }
  } else if (summary->type_name[0] != '\0') {
    if (summary->input_name[0] != '\0') {
      snprintf(buf, len, "%s: %s", summary->input_name, summary->type_name);
    } else {
      snprintf(buf, len, "%s", summary->type_name);
    }
  }
}

// ============================================================================
// Scene inspect: multi-line pad blocks
// ============================================================================

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} ainspect_buf_t;

static bool ainspect_append(ainspect_buf_t *b, const char *fmt, ...) {
  if (!b || !b->buf || b->cap == 0) return false;
  if (b->len >= b->cap) return false;

  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
  va_end(ap);

  if (n < 0) return false;
  if ((size_t)n >= b->cap - b->len) {
    b->len = b->cap - 1;
    b->buf[b->len] = '\0';
    return false;
  }

  b->len += (size_t)n;
  return true;
}

static bool ainspect_control_cc_assigned(const device_def_t *device, uint8_t cc) {
  if (cc > 127) return false;
  if (!device) return false;
  return assets_get_control_by_cc(device, cc) != NULL;
}

static const char *ainspect_polarity_name(polarity_t pol) {
  switch (pol) {
    case POLARITY_BIPOLAR: return "Bipolar";
    case POLARITY_INVERTED: return "Inverted";
    default: return "Unipolar";
  }
}

static const char *ainspect_family_name(action_type_t type) {
  switch (type) {
    case ACTION_CONTROL: return "Control Change";
    case ACTION_NOTE: return "Notes";
    case ACTION_TEMPO: return "Tempo";
    case ACTION_PRESET: return "Preset";
    case ACTION_SCENE: return "Scene";
    case ACTION_TRANSPORT: return "Transport";
    case ACTION_LFO: return "LFO";
    case ACTION_TOUCHWHEEL: return "Touchwheel";
    case ACTION_CLOCK: return "Clock";
    case ACTION_CUT: return "Cut";
    case ACTION_UI: return "UI";
    case ACTION_PARAM: return "Param";
    case ACTION_RTG: return "RTG";
    case ACTION_SAMPLE_HOLD: return "S+H";
    case ACTION_PIANO_PEDAL: return "Piano Pedal";
    case ACTION_RANDOMIZE: return "Randomize";
    case ACTION_BOOMERANG: return "Boomerang";
    case ACTION_INSPECT_SCENE: return "Inspect Scene";
    default: return action_type_to_string(type);
  }
}

const char *action_summary_inspect_family_name(action_type_t type) {
  return ainspect_family_name(type);
}

static void ainspect_cc_label(char *buf, size_t cap, const device_def_t *device, uint8_t cc) {
  const char *name = device ? assets_get_cc_name(device, cc) : NULL;
  if (name && strcmp(name, "Undefined") != 0) {
    snprintf(buf, cap, "%s", name);
  } else {
    snprintf(buf, cap, "CC %u", (unsigned)cc);
  }
}

static void ainspect_value_label(char *buf, size_t cap, const device_def_t *device,
  uint8_t cc, uint8_t value) {
  const char *name = device ? assets_get_discrete_name(device, cc, value) : NULL;
  if (name) {
    snprintf(buf, cap, "%s", name);
  } else {
    snprintf(buf, cap, "%u", (unsigned)value);
  }
}

static const char *ainspect_morph_steps_name(morph_steps_mode_t mode) {
  static const char *names[] = {
    "Auto", "Coarse (8)", "Medium (16)", "Fine (32)", "Manual"
  };
  if (mode > MORPH_STEPS_MANUAL) return "Auto";
  return names[mode];
}

static const char *ainspect_repeat_label(action_repeat_division_t div) {
  switch (div) {
    case ACTION_REPEAT_16_BARS: return "Every 16 bars";
    case ACTION_REPEAT_12_BARS: return "Every 12 bars";
    case ACTION_REPEAT_8_BARS: return "Every 8 bars";
    case ACTION_REPEAT_4_BARS: return "Every 4 bars";
    case ACTION_REPEAT_2_BARS: return "Every 2 bars";
    case ACTION_REPEAT_1_BAR: return "Every bar";
    case ACTION_REPEAT_HALF: return "Every 1/2 note";
    case ACTION_REPEAT_QUARTER: return "Every 1/4 note";
    case ACTION_REPEAT_EIGHTH: return "Every 1/8 note";
    case ACTION_REPEAT_SIXTEENTH: return "Every 1/16 note";
    case ACTION_REPEAT_32ND: return "Every 1/32 note";
    default: return "Every 1/4 note";
  }
}

static bool ainspect_format_timing(char *buf, size_t cap, const action_t *action) {
  if (!action || action->timing == ACTION_TIMING_IMMEDIATE) return false;

  switch (action->timing) {
    case ACTION_TIMING_NEXT_BEAT:
      snprintf(buf, cap, "Next Beat");
      return true;
    case ACTION_TIMING_SPECIFIC_BEAT:
      snprintf(buf, cap, "Beat %u", (unsigned)action->timing_beat);
      return true;
    case ACTION_TIMING_TRANSPORT_START:
      snprintf(buf, cap, "On Transport");
      return true;
    default:
      return false;
  }
}

static bool ainspect_action_is_singleton(action_type_t type) {
  switch (type) {
    case ACTION_RESET:
    case ACTION_CONFIRM_PENDING:
    case ACTION_INSPECT_SCENE:
    case ACTION_FLAG_CEREMONY:
    case ACTION_PUNCH_IN:
    case ACTION_RANDOMIZE:
    case ACTION_BOOMERANG:
      return true;
    default:
      return false;
  }
}

static bool ainspect_format_variant_line(const action_t *action, uint8_t scene_index,
  char *buf, size_t cap) {
  if (!action || !buf || cap == 0) return false;
  buf[0] = '\0';

  if (ainspect_action_is_singleton(action->type)) return false;

  const char *variant = action_variant_to_string(action->variant);
  if (!variant || variant[0] == '\0') variant = "Set";

  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);

  if (action->type == ACTION_CONTROL && action->params.control.num_ccs > 0) {
    uint8_t cc = action->params.control.cc_numbers[0];
    char cc_name[32];
    ainspect_cc_label(cc_name, sizeof(cc_name), device, cc);

    if (action->variant == VARIANT_CYCLE) {
      uint8_t steps = action->params.control.num_cycle_steps;
      if (steps < 2) steps = 2;
      if (steps > 8) steps = 8;

      int pos = snprintf(buf, cap, "Cycle: %s", cc_name);
      for (uint8_t i = 0; i < steps && pos > 0 && (size_t)pos < cap; i++) {
        char val[24];
        ainspect_value_label(val, sizeof(val), device, cc,
          action->params.control.cycle_values[0][i]);
        pos += snprintf(buf + pos, cap - (size_t)pos, "%s%s",
          (i == 0) ? " " : ", ", val);
      }
      return pos > 0;
    }

    if (action->variant == VARIANT_HOLD) {
      char press[24], release[24];
      ainspect_value_label(press, sizeof(press), device, cc, action->params.control.values[0]);
      ainspect_value_label(release, sizeof(release), device, cc,
        action->params.control.values2[0]);
      snprintf(buf, cap, "%s: %s %s - %s", variant, cc_name, press, release);
      return true;
    }

  }

  if (action->type == ACTION_TEMPO) {
    if (action->variant == VARIANT_INCREMENT || action->variant == VARIANT_DECREMENT) {
      uint16_t amount_x10 = action->params.tempo.inc_amount;
      if (amount_x10 == 0) amount_x10 = 10;
      char amount_buf[16];
      tempo_format_bpm(amount_buf, sizeof(amount_buf), amount_x10);
      snprintf(buf, cap, "%s: %c%s",
        variant, action->variant == VARIANT_INCREMENT ? '+' : '-', amount_buf);
      return true;
    }
    if (action->variant == VARIANT_SET) {
      if (action->params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM) {
        char lo_buf[16], hi_buf[16];
        tempo_format_bpm(lo_buf, sizeof(lo_buf), action->params.tempo.random_floor);
        tempo_format_bpm(hi_buf, sizeof(hi_buf), action->params.tempo.random_ceiling);
        snprintf(buf, cap, "Set: Random %s-%s", lo_buf, hi_buf);
      } else if (action->params.tempo.bpm == ACTION_TEMPO_BPM_ORIGINAL) {
        snprintf(buf, cap, "Set: Original");
      } else {
        char bpm_buf[16];
        tempo_format_bpm(bpm_buf, sizeof(bpm_buf), action->params.tempo.bpm);
        snprintf(buf, cap, "Set: %s BPM", bpm_buf);
      }
      return true;
    }
    if (action->variant == VARIANT_HOLD) {
      char press_buf[16], release_buf[16];
      tempo_format_bpm(press_buf, sizeof(press_buf), action->params.tempo.press_bpm);
      tempo_format_bpm(release_buf, sizeof(release_buf), action->params.tempo.release_bpm);
      snprintf(buf, cap, "Hold: %s / %s BPM", press_buf, release_buf);
      return true;
    }
    if (action->variant == VARIANT_TAP) {
      snprintf(buf, cap, "Tap");
      return true;
    }
    if (action->variant == VARIANT_DOWNBEAT) {
      snprintf(buf, cap, "Downbeat");
      return true;
    }
  }

  if (action->type == ACTION_LFO) {
    if (action->variant == VARIANT_MODIFY) return false;

    if (action->variant == VARIANT_TOGGLE) {
      uint8_t slot = action->params.lfo.slot;
      if (slot == 3) snprintf(buf, cap, "Toggle: Both");
      else snprintf(buf, cap, "Toggle: LFO %u", (unsigned)slot);
      return true;
    }
    if (action->variant == VARIANT_START || action->variant == VARIANT_STOP) {
      uint8_t slot = action->params.lfo.slot;
      if (slot == 3) snprintf(buf, cap, "%s: Both", variant);
      else snprintf(buf, cap, "%s: LFO %u", variant, (unsigned)slot);
      return true;
    }
  }

  if (action->type == ACTION_RTG && action->variant == VARIANT_MODIFY) return false;
  if (action->type == ACTION_SAMPLE_HOLD && action->variant == VARIANT_MODIFY) return false;

  if (action->type == ACTION_PRESET && action->variant == VARIANT_CYCLE) {
    int pos = snprintf(buf, cap, "Programs:");
    if (pos > 0 && (size_t)pos < cap) {
      char list[64];
      summary_append_preset_cycle_list(list, sizeof(list), action);
      if (list[0] != '\0')
        pos += snprintf(buf + pos, cap - (size_t)pos, " %s", list);
    }
    return pos > 0;
  }

  if (action->type == ACTION_SCENE && action->variant == VARIANT_SET) {
    const char *name = summary_scene_name_for_target(action->params.target.number);
    if (name) snprintf(buf, cap, "Set: %s", name);
    else snprintf(buf, cap, "Set: Scene %u", (unsigned)action->params.target.number + 1);
    return true;
  }

  if (action->type == ACTION_NOTE) {
    if (action->params.note.note == ACTION_NOTE_RANDOM) {
      uint8_t lo = action->params.note.random_floor;
      uint8_t hi = action->params.note.random_ceiling;
      if (lo < 36) lo = 36;
      if (hi > 96) hi = 96;
      if (lo == 36 && hi == 96) {
        snprintf(buf, cap, "Set: Random");
      } else {
        char lo_name[8];
        char hi_name[8];
        summary_note_name(lo, lo_name, sizeof(lo_name));
        summary_note_name(hi, hi_name, sizeof(hi_name));
        snprintf(buf, cap, "Set: Random %s-%s", lo_name, hi_name);
      }
    } else {
      char note_name[8];
      summary_note_name(action->params.note.note, note_name, sizeof(note_name));
      snprintf(buf, cap, "Set: %s", note_name);
    }
    return true;
  }

  action_summary_t summary;
  action_summary_init(&summary);
  action_format_summary(action, &summary, scene_index);
  if (summary.has_param && summary.has_value) {
    snprintf(buf, cap, "%s: %s %s", variant, summary.param_name, summary.param_value);
    return true;
  }
  if (summary.has_param) {
    snprintf(buf, cap, "%s: %s", variant, summary.param_name);
    return true;
  }
  return false;
}

static const char *ainspect_lfo_waveform_name(uint8_t wf) {
  switch ((lfo_waveform_t)wf) {
    case LFO_WAVEFORM_SINE: return "Sine";
    case LFO_WAVEFORM_TRIANGLE: return "Triangle";
    case LFO_WAVEFORM_SQUARE: return "Square";
    case LFO_WAVEFORM_SAW_UP: return "Saw Up";
    case LFO_WAVEFORM_SAW_DOWN: return "Saw Down";
    case LFO_WAVEFORM_SAMPLE_HOLD: return "S&H";
    case LFO_WAVEFORM_BIN: return "Bin";
    case LFO_WAVEFORM_GLIDER: return "Glider";
    case LFO_WAVEFORM_STRAY: return "Stray";
    case LFO_WAVEFORM_CUSTOM: return "Custom";
    default: return "?";
  }
}

static void ainspect_append_lfo_modify_pair(ainspect_buf_t *b, const char *key,
  const char *value) {
  ainspect_append(b, "\n%s: %s", key, value);
}

static void ainspect_append_lfo_modify(ainspect_buf_t *b, const action_t *action) {
  char val[24];

  if (action->params.lfo.waveform != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.waveform == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%s", ainspect_lfo_waveform_name(action->params.lfo.waveform));
    ainspect_append_lfo_modify_pair(b, "Wave", val);
  }
  if (action->params.lfo.rate_mode != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.rate_mode == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%s",
      action->params.lfo.rate_mode == 0 ? "Time" : "Division");
    ainspect_append_lfo_modify_pair(b, "Rate Mode", val);
  }
  if (action->params.lfo.rate_hz_x100 != ACTION_LFO_ORIG_U16) {
    if (action->params.lfo.rate_hz_x100 == ACTION_LFO_RAND_U16) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%.1f Hz", (double)action->params.lfo.rate_hz_x100 / 100.0);
    ainspect_append_lfo_modify_pair(b, "Rate", val);
  }
  if (action->params.lfo.division != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.division == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%u", (unsigned)action->params.lfo.division);
    ainspect_append_lfo_modify_pair(b, "Division", val);
  }
  if (action->params.lfo.floor != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.floor == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%u", (unsigned)action->params.lfo.floor);
    ainspect_append_lfo_modify_pair(b, "Floor", val);
  }
  if (action->params.lfo.ceiling != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.ceiling == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%u", (unsigned)action->params.lfo.ceiling);
    ainspect_append_lfo_modify_pair(b, "Ceiling", val);
  }
  if (action->params.lfo.resolution_mode != ACTION_LFO_ORIG_U8) {
    if (action->params.lfo.resolution_mode == ACTION_LFO_RAND_U8) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%u", (unsigned)action->params.lfo.resolution_mode);
    ainspect_append_lfo_modify_pair(b, "Resolution", val);
  }
  if (action->params.lfo.manual_steps != ACTION_LFO_ORIG_STEPS) {
    if (action->params.lfo.manual_steps == ACTION_LFO_RAND_STEPS) snprintf(val, sizeof(val), "Random");
    else snprintf(val, sizeof(val), "%u", (unsigned)action->params.lfo.manual_steps);
    ainspect_append_lfo_modify_pair(b, "Steps", val);
  }
}

static void ainspect_append_engine_modify(ainspect_buf_t *b,
  const action_engine_modify_t *m) {
  if (!m) return;

  if (m->rate_mode != ACTION_LFO_ORIG_U8) {
    const char *mode = (m->rate_mode == ACTION_LFO_RAND_U8) ? "Random" :
      (m->rate_mode == 0 ? "Time" : "Division");
    ainspect_append(b, "\nRate Mode: %s", mode);
  }
  if (m->rate_hz_x100 != ACTION_LFO_ORIG_U16) {
    if (m->rate_hz_x100 == ACTION_LFO_RAND_U16) ainspect_append(b, "\nRate: Random");
    else ainspect_append(b, "\nRate: %.1f Hz", (double)m->rate_hz_x100 / 100.0);
  }
  if (m->division != ACTION_LFO_ORIG_U8) {
    if (m->division == ACTION_LFO_RAND_U8) ainspect_append(b, "\nDivider: Random");
    else ainspect_append(b, "\nDivider: %u", (unsigned)m->division);
  }
  if (m->glide != ACTION_LFO_ORIG_U8) {
    if (m->glide == ACTION_LFO_RAND_U8) ainspect_append(b, "\nGlide: Random");
    else ainspect_append(b, "\nGlide: %u", (unsigned)m->glide);
  }
  if (m->probability != ACTION_LFO_ORIG_U8) {
    if (m->probability == ACTION_LFO_RAND_U8) ainspect_append(b, "\nProbability: Random");
    else ainspect_append(b, "\nProbability: %u%%", (unsigned)m->probability);
  }
}

static void ainspect_append_pattern(ainspect_buf_t *b, const action_t *action) {
  uint8_t length = action->pattern_length;
  if (length < 2) return;

  char pattern[12];
  for (uint8_t i = 0; i < length && i < 8; i++) {
    pattern[i] = (action->pattern_mask & (1u << i)) ? 'X' : '.';
  }
  pattern[length] = '\0';
  ainspect_append(b, "\nPattern: %s", pattern);
}

static void ainspect_append_control_set_lines(ainspect_buf_t *b, const action_t *action,
  uint8_t scene_index) {
  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
  uint8_t num = action->params.control.num_ccs;
  if (num > 4) num = 4;

  bool any = false;
  for (uint8_t i = 0; i < num; i++) {
    uint8_t cc = action->params.control.cc_numbers[i];
    if (!ainspect_control_cc_assigned(device, cc)) continue;

    char cc_name[32];
    char val[24];
    ainspect_cc_label(cc_name, sizeof(cc_name), device, cc);
    ainspect_value_label(val, sizeof(val), device, cc, action->params.control.values[i]);
    ainspect_append(b, "\nSet: %s %s", cc_name, val);
    any = true;
  }
  if (!any) ainspect_append(b, "\nSet: Unassigned!");
}

static const char *ainspect_boomerang_division_label(uint8_t division) {
  switch ((morph_division_t)division) {
    case MORPH_DIV_1_BEAT: return "1 beat";
    case MORPH_DIV_1_BAR: return "1 bar";
    case MORPH_DIV_2_BARS: return "2 bars";
    case MORPH_DIV_4_BARS: return "4 bars";
    case MORPH_DIV_BEAT_2: return "Beat 2";
    case MORPH_DIV_BEAT_3: return "Beat 3";
    case MORPH_DIV_BEAT_4: return "Beat 4";
    case MORPH_DIV_2_BEATS: return "2 beats";
    case MORPH_DIV_3_BEATS: return "3 beats";
    case MORPH_DIV_3_BARS: return "3 bars";
    default: return "1 bar";
  }
}

static const char *ainspect_repeat_interval_label(action_repeat_division_t div) {
  switch (div) {
    case ACTION_REPEAT_16_BARS: return "16 bars";
    case ACTION_REPEAT_12_BARS: return "12 bars";
    case ACTION_REPEAT_8_BARS: return "8 bars";
    case ACTION_REPEAT_4_BARS: return "4 bars";
    case ACTION_REPEAT_2_BARS: return "2 bars";
    case ACTION_REPEAT_1_BAR: return "1 bar";
    case ACTION_REPEAT_HALF: return "1/2 note";
    case ACTION_REPEAT_QUARTER: return "1/4 note";
    case ACTION_REPEAT_EIGHTH: return "1/8 note";
    case ACTION_REPEAT_SIXTEENTH: return "1/16 note";
    case ACTION_REPEAT_32ND: return "1/32 note";
    default: return "1 bar";
  }
}

static const char *ainspect_punch_in_duration_label(punch_in_duration_t duration) {
  switch (duration) {
    case PUNCH_IN_1_BEAT:  return "1 beat";
    case PUNCH_IN_2_BEATS: return "2 beats";
    case PUNCH_IN_3_BEATS: return "3 beats";
    case PUNCH_IN_4_BEATS: return "4 beats";
    case PUNCH_IN_5_BEATS: return "5 beats";
    case PUNCH_IN_6_BEATS: return "6 beats";
    case PUNCH_IN_7_BEATS: return "7 beats";
    case PUNCH_IN_1_BAR:   return "1 bar";
    case PUNCH_IN_2_BARS:  return "2 bars";
    case PUNCH_IN_4_BARS:  return "4 bars";
    case PUNCH_IN_8_BARS:  return "8 bars";
    case PUNCH_IN_16_BARS: return "16 bars";
    default: return "1 bar";
  }
}

static void ainspect_boomerang_param_name(char *buf, size_t cap, const action_t *action,
    uint8_t scene_index) {
  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
  if (action->params.boomerang.output_type == OUTPUT_TYPE_CC) {
    ainspect_cc_label(buf, cap, device, action->params.boomerang.cc_number);
  } else {
    const char *out_name = output_type_name((output_type_t)action->params.boomerang.output_type);
    snprintf(buf, cap, "%s", out_name ? out_name : "Output");
  }
}

static void ainspect_boomerang_phase_duration(char *buf, size_t cap, uint8_t mode,
    uint16_t time_ms, uint8_t division) {
  switch (mode) {
    case BOOMERANG_DUR_INSTANT:
      snprintf(buf, cap, "0ms");
      break;
    case BOOMERANG_DUR_TIME_MS:
      snprintf(buf, cap, "%ums", (unsigned)time_ms);
      break;
    case BOOMERANG_DUR_DIVISION:
      snprintf(buf, cap, "%s", ainspect_boomerang_division_label(division));
      break;
    default:
      snprintf(buf, cap, "0ms");
      break;
  }
}

static void ainspect_append_boomerang(ainspect_buf_t *b, const action_t *action,
    bool show_scheduling) {
  if (action->params.boomerang.start_mode == BOOMERANG_START_CURRENT) {
    ainspect_append(b, "\nOrigin: Current");
  } else {
    ainspect_append(b, "\nOrigin: %u", (unsigned)action->params.boomerang.start_value);
  }

  if (action->params.boomerang.target_mode == BOOMERANG_TARGET_RANDOM) {
    ainspect_append(b, " End: Random");
  } else {
    ainspect_append(b, " End: %u", (unsigned)action->params.boomerang.target_value);
  }

  char atk[24], sus[24], dec[24];
  ainspect_boomerang_phase_duration(atk, sizeof(atk), action->params.boomerang.attack_mode,
    action->params.boomerang.attack_time_ms, action->params.boomerang.attack_division);
  ainspect_boomerang_phase_duration(sus, sizeof(sus), action->params.boomerang.sustain_mode,
    action->params.boomerang.sustain_time_ms, action->params.boomerang.sustain_division);
  ainspect_boomerang_phase_duration(dec, sizeof(dec), action->params.boomerang.release_mode,
    action->params.boomerang.release_time_ms, action->params.boomerang.release_division);
  ainspect_append(b, "\nAtk: %s Sus: %s Dec: %s", atk, sus, dec);

  char timing[24];
  if (show_scheduling && ainspect_format_timing(timing, sizeof(timing), action)) {
    ainspect_append(b, "\nTiming: %s", timing);
  }

  if (show_scheduling && action_supports_repeat_for(action) && action->repeat_enabled) {
    ainspect_append(b, "\nRepeat: %s",
      ainspect_repeat_interval_label(action->repeat_division));
  }
}

static void ainspect_append_action_options(ainspect_buf_t *b, const action_t *action,
    bool show_scheduling) {
  if (action_supports_morph_for(action) && action->morph_enabled) {
    ainspect_append(b, "\nMorph: %s", ainspect_morph_steps_name(action->morph_steps_mode));
  }

  if (show_scheduling && action_supports_repeat_for(action) && action->repeat_enabled) {
    ainspect_append(b, "\nRepeat: %s", ainspect_repeat_label(action->repeat_division));

    uint8_t prob = action->probability;
    if (prob == 0) prob = 100;
    if (prob < 100) ainspect_append(b, "\nProbability: %u%%", (unsigned)prob);

    ainspect_append_pattern(b, action);
  }

  char timing[24];
  if (show_scheduling && action_supports_timing_for(action) &&
      ainspect_format_timing(timing, sizeof(timing), action)) {
    ainspect_append(b, "\nTiming: %s", timing);
  }

  if (action_supports_raise_flag_for(action) && action->raise_flag) {
    ainspect_append(b, "\nRaise the Flag");
  }
}

static void ainspect_append_action_body(ainspect_buf_t *out, const action_t *action,
  uint8_t scene_index, bool show_scheduling) {
  if (action->type == ACTION_BOOMERANG) {
    ainspect_append_boomerang(out, action, show_scheduling);
    return;
  }

  if (action->type == ACTION_PUNCH_IN) {
    ainspect_append(out, "\n%s",
      ainspect_punch_in_duration_label(action->params.punch_in.duration));
  }

  if (action->type == ACTION_CONTROL && action->variant == VARIANT_SET) {
    ainspect_append_control_set_lines(out, action, scene_index);
  } else {
    char variant_line[192];
    if (ainspect_format_variant_line(action, scene_index, variant_line, sizeof(variant_line))) {
      ainspect_append(out, "\n%s", variant_line);
    } else if (action->type == ACTION_CONTROL && action->variant == VARIANT_SET) {
      ainspect_append(out, "\nSet: Unassigned!");
    }
  }

  if (action->type == ACTION_LFO && action->variant == VARIANT_MODIFY) {
    ainspect_append_lfo_modify(out, action);
  } else if (action->type == ACTION_RTG && action->variant == VARIANT_MODIFY) {
    ainspect_append_engine_modify(out, &action->params.rtg_modify);
  } else if (action->type == ACTION_SAMPLE_HOLD && action->variant == VARIANT_MODIFY) {
    ainspect_append_engine_modify(out, &action->params.sh_modify);
  }

  ainspect_append_action_options(out, action, show_scheduling);
}

bool action_summary_format_inspect_action_body(const action_t *action, uint8_t scene_index,
  char *buf, size_t len) {
  if (!action || action->type == ACTION_NONE || !buf || len == 0) return false;

  ainspect_buf_t out = { .buf = buf, .cap = len, .len = 0 };
  buf[0] = '\0';
  ainspect_append_action_body(&out, action, scene_index, true);
  return out.len > 0;
}

bool action_summary_format_inspect_jack_line(const action_t *action, uint8_t scene_index,
  char *buf, size_t len) {
  if (!action || action->type == ACTION_NONE || !buf || len == 0) return false;
  buf[0] = '\0';

  const char *family = ainspect_family_name(action->type);

  if (action->type == ACTION_CONTROL && action->variant == VARIANT_SET) {
    char detail[256];
    ainspect_buf_t out = { .buf = detail, .cap = sizeof(detail), .len = 0 };
    ainspect_append_control_set_lines(&out, action, scene_index);
    if (detail[0] == '\n') {
      snprintf(buf, len, "%s%s", family, detail);
    } else if (detail[0] != '\0') {
      snprintf(buf, len, "%s: %s", family, detail);
    } else {
      snprintf(buf, len, "%s", family);
    }
    return true;
  }

  char detail[192];
  if (ainspect_format_variant_line(action, scene_index, detail, sizeof(detail))) {
    snprintf(buf, len, "%s: %s", family, detail);
    return true;
  }

  snprintf(buf, len, "%s", family);
  return true;
}

bool action_summary_format_inspect_continuous(const continuous_mapping_t *mapping,
  uint8_t scene_index, char *buf, size_t len) {
  if (!mapping || !mapping->enabled || !buf || len == 0) return false;

  ainspect_buf_t out = { .buf = buf, .cap = len, .len = 0 };
  buf[0] = '\0';

  const char *type_name = output_type_name(mapping->output_type);
  ainspect_append(&out, "%s", type_name);

  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);

  if (mapping->output_type == OUTPUT_TYPE_CC) {
    char cc_name[32];
    if (ainspect_control_cc_assigned(device, mapping->cc_number)) {
      ainspect_cc_label(cc_name, sizeof(cc_name), device, mapping->cc_number);
      ainspect_append(&out, "\n%s %u - %u", cc_name,
        (unsigned)mapping->min_value, (unsigned)mapping->max_value);
    } else {
      ainspect_append(&out, "\nUnassigned!");
    }
  } else if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    ainspect_append(&out, "\nBase %u, Range %u",
      (unsigned)mapping->base_note, (unsigned)mapping->note_range);
  } else if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    ainspect_append(&out, "\nNudge around scene BPM");
  } else if (mapping->output_type == OUTPUT_TYPE_PITCH_BEND) {
    ainspect_append(&out, "\nPitch bend");
  } else {
    action_summary_t summary;
    action_summary_init(&summary);
    continuous_format_summary(mapping, &summary, scene_index);
    if (summary.has_param) ainspect_append(&out, "\n%s", summary.param_name);
    if (summary.has_value) ainspect_append(&out, " %s", summary.param_value);
  }

  ainspect_append(&out, "\nCurve: %s", curve_type_to_string(mapping->curve.type));
  ainspect_append(&out, "\nPolarity: %s", ainspect_polarity_name(mapping->polarity));

  return out.len > 0;
}

static bool ainspect_format_inspect_headline(const char *pad_name, const action_t *action,
  uint8_t scene_index, char *buf, size_t cap) {
  if (!pad_name || !action || !buf || cap == 0) return false;

  if (action->type == ACTION_SCENE) {
    if (action->variant == VARIANT_INCREMENT) {
      snprintf(buf, cap, "%s: Scene +1", pad_name);
      return true;
    }
    if (action->variant == VARIANT_DECREMENT) {
      snprintf(buf, cap, "%s: Scene -1", pad_name);
      return true;
    }
  }

  if (action->type == ACTION_SAMPLE_HOLD && action->variant == VARIANT_MODIFY) {
    snprintf(buf, cap, "%s: S+H: Modify", pad_name);
    return true;
  }

  if (action->type == ACTION_BOOMERANG) {
    char param[32];
    ainspect_boomerang_param_name(param, sizeof(param), action, scene_index);
    snprintf(buf, cap, "%s: Boomerang: %s", pad_name, param);
    return true;
  }

  return false;
}

bool action_summary_format_inspect_pad(const action_t *action, const char *pad_name,
  uint8_t scene_index, char *buf, size_t len, bool show_scheduling) {
  if (!action || action->type == ACTION_NONE || !pad_name || !buf || len == 0) return false;

  ainspect_buf_t out = { .buf = buf, .cap = len, .len = 0 };
  buf[0] = '\0';

  char headline[96];
  if (ainspect_format_inspect_headline(pad_name, action, scene_index, headline, sizeof(headline)))
    ainspect_append(&out, "%s", headline);
  else
    ainspect_append(&out, "%s: %s", pad_name, ainspect_family_name(action->type));
  ainspect_append_action_body(&out, action, scene_index, show_scheduling);

  return out.len > 0;
}

void action_summary_format_display(const action_summary_t *summary,
    char *buf, size_t len, uint32_t input_color) {
  if (!summary || !buf || len == 0) return;

  uint8_t r = (input_color >> 16) & 0xFF;
  uint8_t g = (input_color >> 8) & 0xFF;
  uint8_t b = input_color & 0xFF;

  if (summary->has_param && summary->has_value) {
    snprintf(buf, len, "#%02X%02X%02X %s#\n%s\n%s\n%s",
      r, g, b,
      summary->input_name,
      summary->type_name,
      summary->param_name,
      summary->param_value);
  } else if (summary->has_param) {
    snprintf(buf, len, "#%02X%02X%02X %s#\n%s\n%s",
      r, g, b,
      summary->input_name,
      summary->type_name,
      summary->param_name);
  } else {
    snprintf(buf, len, "#%02X%02X%02X %s#\n%s",
      r, g, b,
      summary->input_name,
      summary->type_name);
  }
}
