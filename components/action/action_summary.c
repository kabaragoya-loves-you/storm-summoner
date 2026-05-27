#include "action_summary.h"
#include "assets_manager.h"
#include "touchwheel_mode_mapping.h"
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
      snprintf(summary->input_name, sizeof(summary->input_name), "Sample+Hold");
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
        snprintf(summary->param_name, sizeof(summary->param_name), "Note Random");
      } else {
        snprintf(summary->param_name, sizeof(summary->param_name), "Note Random %u-%u",
          (unsigned)lo, (unsigned)hi);
      }
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "Note %u",
        (unsigned)action->params.note.note);
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
    snprintf(summary->param_value, sizeof(summary->param_value), "%u values",
      (unsigned)action->params.preset.num_presets);
    summary->has_value = true;

  } else if (action->type == ACTION_SCENE && action->variant == VARIANT_SET) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Scene");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u",
      (unsigned)action->params.target.number + 1);
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO && action->variant == VARIANT_SET) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    if (action->params.tempo.bpm == ACTION_TEMPO_BPM_ORIGINAL) {
      snprintf(summary->param_value, sizeof(summary->param_value), "Original");
    } else if (action->params.tempo.bpm == ACTION_TEMPO_BPM_RANDOM) {
      uint16_t lo = action->params.tempo.random_floor;
      uint16_t hi = action->params.tempo.random_ceiling;
      if (lo < 20 || lo > 300) lo = 20;
      if (hi < 20 || hi > 300) hi = 300;
      if (lo > hi) hi = lo;
      snprintf(summary->param_value, sizeof(summary->param_value), "Random %u-%u",
        (unsigned)lo, (unsigned)hi);
    } else {
      snprintf(summary->param_value, sizeof(summary->param_value), "%u",
        (unsigned)action->params.tempo.bpm);
    }
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO && action->variant == VARIANT_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u / %u",
      (unsigned)action->params.tempo.press_bpm,
      (unsigned)action->params.tempo.release_bpm);
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
    uint8_t amount = action->params.tempo.inc_amount;
    if (amount == 0) amount = 1;
    snprintf(summary->param_name, sizeof(summary->param_name), "Step");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%c%u BPM",
      action->variant == VARIANT_INCREMENT ? '+' : '-', (unsigned)amount);
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
    // / Pol / Floor / Ceil / Res / Steps). Tags that wouldn't fit are
    // silently dropped rather than truncating mid-tag.
    if (action->variant == VARIANT_MODIFY) {
      // Static lookup keeps the per-call code small and easy to scan.
      static const struct { uint8_t field; const char *tag; } overrides[] = {
        { 0, "Wave"     },
        { 1, "RateMode" },
        { 2, "Rate"     },
        { 3, "Div"      },
        { 4, "Pol"      },
        { 5, "Floor"    },
        { 6, "Ceil"     },
        { 7, "Res"      },
        { 8, "Steps"    },
      };
      bool active[9] = {
        action->params.lfo.waveform        != ACTION_LFO_ORIG_U8,
        action->params.lfo.rate_mode       != ACTION_LFO_ORIG_U8,
        action->params.lfo.rate_hz_x100    != ACTION_LFO_ORIG_U16,
        action->params.lfo.division        != ACTION_LFO_ORIG_U8,
        action->params.lfo.polarity        != ACTION_LFO_ORIG_U8,
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
