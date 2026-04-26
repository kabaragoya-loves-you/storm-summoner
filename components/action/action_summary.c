#include "action_summary.h"
#include "assets_manager.h"
#include "touchwheel_mode_mapping.h"
#include <string.h>
#include <stdio.h>

// Display name for action types (user-friendly names)
static const char* get_action_display_name(action_type_t type) {
  switch (type) {
    case ACTION_NONE: return "Undefined";
    case ACTION_CONTROL_CHANGE: return "Control Change";
    case ACTION_CONTROL_HOLD: return "Control Hold";
    case ACTION_CONTROL_CYCLE: return "Control Cycle";
    case ACTION_PRESET_INC: return "Preset +1";
    case ACTION_PRESET_DEC: return "Preset -1";
    case ACTION_PRESET: return "Set Preset";
    case ACTION_PRESET_HOLD: return "Preset Hold";
    case ACTION_PRESET_CYCLE: return "Preset Cycle";
    case ACTION_SCENE_INC: return "Scene +1";
    case ACTION_SCENE_DEC: return "Scene -1";
    case ACTION_SCENE: return "Set Scene";
    case ACTION_PLAY: return "Play";
    case ACTION_STOP: return "Stop";
    case ACTION_PAUSE: return "Pause";
    case ACTION_RECORD: return "Record";
    case ACTION_TAP: return "Tap";
    case ACTION_TAP_TEMPO: return "Tap Tempo";
    case ACTION_SET_TEMPO: return "Set Tempo";
    case ACTION_TEMPO_INC: return "Tempo +1";
    case ACTION_TEMPO_DEC: return "Tempo -1";
    case ACTION_TEMPO_HOLD: return "Tempo Hold";
    case ACTION_TEMPO_CYCLE: return "Tempo Cycle";
    case ACTION_NOTE: return "Note";
    case ACTION_RANDOMIZE: return "Randomize";
    case ACTION_CONFIRM_PENDING: return "Confirm Pending";
    case ACTION_RESET: return "Reset";
    case ACTION_SUSTAIN: return "Sustain";
    case ACTION_SOSTENUTO: return "Sostenuto";
    case ACTION_TOUCHWHEEL_HOLD: return "Touchwheel Hold";
    case ACTION_TOUCHWHEEL_CYCLE: return "Touchwheel Cycle";
    case ACTION_LFO_START: return "LFO Start";
    case ACTION_LFO_STOP: return "LFO Stop";
    case ACTION_LFO_TOGGLE: return "LFO Toggle";
    case ACTION_LFO_SHAPE: return "LFO Shape";
    case ACTION_CLOCK_TOGGLE: return "Clock Toggle";
    case ACTION_CLOCK_HOLD: return "Clock Hold";
    case ACTION_CLOCK_BURST: return "Clock Burst";
    case ACTION_CUT_TOGGLE: return "Cut Toggle";
    case ACTION_CUT_HOLD: return "Cut Hold";
    case ACTION_SET_UI: return "Set UI";
    case ACTION_UI_HOLD: return "UI Hold";
    case ACTION_UI_CYCLE: return "UI Cycle";
    case ACTION_PARAM_HOLD: return "Param Hold";
    case ACTION_PARAM_CYCLE: return "Param Cycle";
    case ACTION_RTG_TOGGLE: return "RTG Toggle";
    case ACTION_RTG_HOLD: return "RTG Hold";
    case ACTION_SAMPLE_HOLD_TOGGLE: return "S+H Toggle";
    case ACTION_SAMPLE_HOLD_HOLD: return "S+H Hold";
    case ACTION_STEP: return "Step";
    case ACTION_PUNCH_IN: return "Punch-In";
    case ACTION_FLAG_CEREMONY: return "Flag Ceremony";
    case ACTION_BOOMERANG: return "Boomerang";
    default: return "Unknown";
  }
}

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

  const char *type_name = get_action_display_name(action->type);
  snprintf(summary->type_name, sizeof(summary->type_name), "%s", type_name);

  // For Control Change actions, show the first CC slot detail
  if (action->type == ACTION_CONTROL_CHANGE && action->params.control.num_ccs > 0) {
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

  } else if (action->type == ACTION_CONTROL_HOLD && action->params.control.num_ccs > 0) {
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

  } else if (action->type == ACTION_CONTROL_CYCLE && action->params.control.num_ccs > 0) {
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
    snprintf(summary->param_name, sizeof(summary->param_name), "Note %u",
      (unsigned)action->params.note.note);
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "Vel %u",
      (unsigned)action->params.note.velocity);
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Program");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u",
      (unsigned)action->params.preset.program + 1);
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Programs");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u / %u",
      (unsigned)action->params.preset_cycle.press_preset + 1,
      (unsigned)action->params.preset_cycle.release_preset + 1);
    summary->has_value = true;

  } else if (action->type == ACTION_PRESET_CYCLE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Programs");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u values",
      (unsigned)action->params.preset_cycle.num_presets);
    summary->has_value = true;

  } else if (action->type == ACTION_SCENE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "Scene");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u",
      (unsigned)action->params.target.number + 1);
    summary->has_value = true;

  } else if (action->type == ACTION_SET_TEMPO) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u",
      (unsigned)action->params.tempo.bpm);
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO_HOLD) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u / %u",
      (unsigned)action->params.tempo.press_bpm,
      (unsigned)action->params.tempo.release_bpm);
    summary->has_value = true;

  } else if (action->type == ACTION_TEMPO_CYCLE) {
    snprintf(summary->param_name, sizeof(summary->param_name), "BPM");
    summary->has_param = true;
    snprintf(summary->param_value, sizeof(summary->param_value), "%u values",
      (unsigned)action->params.tempo.num_tempos);
    summary->has_value = true;

  } else if (action->type == ACTION_LFO_START || action->type == ACTION_LFO_STOP ||
             action->type == ACTION_LFO_TOGGLE) {
    uint8_t slot = action->params.lfo.slot;
    if (slot == 3) {
      snprintf(summary->param_name, sizeof(summary->param_name), "Both LFOs");
    } else {
      snprintf(summary->param_name, sizeof(summary->param_name), "LFO %u",
        (unsigned)slot);
    }
    summary->has_param = true;

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
