#include "scene_inspect.h"
#include "action_summary.h"
#include "assets_manager.h"
#include "device_config.h"
#include "midi_control.h"
#include "expression.h"
#include "cv.h"
#include "input_mode.h"
#include "rtg.h"
#include "lfo.h"
#include "sample_hold.h"
#include "ui.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SCENE_INSPECT_TRUNC_MARKER "\n…"

void scene_inspect_buf_init(scene_inspect_buf_t *b, char *buf, size_t cap) {
  if (!b) return;
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  b->truncated = false;
  if (buf && cap > 0) buf[0] = '\0';
}

bool scene_inspect_buf_append(scene_inspect_buf_t *b, const char *fmt, ...) {
  if (!b || !b->buf || b->cap == 0 || !fmt) return false;
  if (b->truncated) return false;

  size_t remain = b->cap - b->len;
  if (remain <= 1) {
    b->truncated = true;
    return false;
  }

  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b->buf + b->len, remain, fmt, ap);
  va_end(ap);

  if (n < 0) {
    b->truncated = true;
    return false;
  }

  if ((size_t)n >= remain) {
    b->len = b->cap - 1;
    b->buf[b->len] = '\0';
    b->truncated = true;
    return false;
  }

  b->len += (size_t)n;
  return true;
}

static void append_truncation_marker(scene_inspect_buf_t *b) {
  if (!b->truncated) return;
  size_t marker_len = strlen(SCENE_INSPECT_TRUNC_MARKER);
  if (b->len + marker_len >= b->cap) {
    if (b->cap > marker_len) {
      memcpy(b->buf + b->cap - marker_len - 1, SCENE_INSPECT_TRUNC_MARKER, marker_len);
      b->buf[b->cap - 1] = '\0';
      b->len = b->cap - 1;
    }
    return;
  }
  scene_inspect_buf_append(b, "%s", SCENE_INSPECT_TRUNC_MARKER);
}

static uint16_t scene_inspect_device_index_base(uint8_t scene_index) {
  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
  if (device && device->pc_info) return device->pc_info->index_base;
  return 0;
}

static const char *clock_source_name(tempo_clock_source_t src) {
  switch (src) {
    case CLOCK_SOURCE_INTERNAL: return "Internal";
    case CLOCK_SOURCE_MIDI: return "MIDI";
    case CLOCK_SOURCE_SYNC: return "Sync";
    default: return "?";
  }
}

static const char *beat_divider_name(tempo_note_divider_t div) {
  switch (div) {
    case DIVIDER_QUARTER: return "Quarter Notes";
    case DIVIDER_EIGHTH: return "Eighth Notes";
    case DIVIDER_SIXTEENTH: return "Sixteenth Notes";
    default: return "?";
  }
}

static const char *trs_type_name(uint8_t trs) {
  switch (trs) {
    case 1: return "Type A";
    case 2: return "Type B";
    case 3: return "TS";
    case 4: return "Both";
    default: return "?";
  }
}

static void append_summary_line(scene_inspect_buf_t *b, const action_summary_t *summary) {
  char line[160];
  action_summary_format_line(summary, line, sizeof(line));
  if (line[0] != '\0') scene_inspect_buf_append(b, "%s\n\n", line);
}

static void append_inspect_action(scene_inspect_buf_t *b, const char *label,
  const action_t *action, uint8_t scene_index, bool trailing_blank, bool show_scheduling) {
  if (!action || action->type == ACTION_NONE || !label) return;

  char block[768];
  if (!action_summary_format_inspect_pad(action, label, scene_index, block, sizeof(block),
      show_scheduling)) return;

  scene_inspect_buf_append(b, "%s%s", block, trailing_blank ? "\n\n" : "\n");
}

static const char *jack_connection_status(bool connected) {
  return connected ? "Connected" : "Disconnected";
}

static void append_jack_action_block(scene_inspect_buf_t *b, const char *jack_label,
  bool connected, const action_t *action, uint8_t scene_index) {
  scene_inspect_buf_append(b, "%s: %s\n", jack_label, jack_connection_status(connected));

  if (action && action->type != ACTION_NONE) {
    scene_inspect_buf_append(b, "%s\n", action_summary_inspect_family_name(action->type));
    char body[512];
    if (action_summary_format_inspect_action_body(action, scene_index, body, sizeof(body))) {
      scene_inspect_buf_append(b, "%s\n", body);
    }
  }

  scene_inspect_buf_append(b, "\n");
}

static void append_pedal_enabled_block(scene_inspect_buf_t *b, const char *label,
  bool connected) {
  scene_inspect_buf_append(b, "%s: %s\n\n", label, jack_connection_status(connected));
}

static void append_continuous_assignment(scene_inspect_buf_t *b, const char *prefix,
  const continuous_mapping_t *mapping, uint8_t scene_index);

static void append_cv_mapping_block(scene_inspect_buf_t *b, const char *label,
  bool connected, const continuous_mapping_t *mapping, uint8_t scene_index,
  uint8_t nudge_pct) {
  if (!mapping || !mapping->enabled) {
    scene_inspect_buf_append(b, "%s: %s\n\n", label, jack_connection_status(connected));
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    scene_inspect_buf_append(b, "%s: Tempo\n", label);
    scene_inspect_buf_append(b, "Nudge %u%%\n\n", (unsigned)nudge_pct);
    return;
  }

  scene_inspect_buf_append(b, "%s: %s\n", label, jack_connection_status(connected));
  append_continuous_assignment(b, "Set", mapping, scene_index);
  scene_inspect_buf_append(b, "\n");
}

static const char *cv_velocity_inspect_label(const scene_t *scene) {
  static char fixed_vel[16];
  switch (scene->cv_velocity_mode) {
    case VELOCITY_MODE_GATE_VOLTAGE: return "Gate Voltage";
    case VELOCITY_MODE_TOUCHWHEEL: return "Touchwheel";
    case VELOCITY_MODE_PROXIMITY: return "Proximity";
    case VELOCITY_MODE_ALS: return "ALS";
    case VELOCITY_MODE_TILT_X: return "Tilt X";
    case VELOCITY_MODE_TILT_Y: return "Tilt Y";
    case VELOCITY_MODE_LFO1: return "LFO 1";
    case VELOCITY_MODE_LFO2: return "LFO 2";
    case VELOCITY_MODE_SAMPLE_HOLD: return "S+H";
    default:
      fixed_vel[0] = '\0';
      {
        uint8_t vel = scene->cv_velocity;
        if (vel == 0) vel = 100;
        snprintf(fixed_vel, sizeof(fixed_vel), "%u", (unsigned)vel);
      }
      return fixed_vel;
  }
}

static const char *scene_inspect_output_type_name(output_type_t type) {
  switch (type) {
    case OUTPUT_TYPE_CC: return "Control Change";
    case OUTPUT_TYPE_NOTE: return "Notes";
    case OUTPUT_TYPE_LFO_RATE: return "LFO Rate";
    case OUTPUT_TYPE_LFO_DEPTH: return "LFO Depth";
    case OUTPUT_TYPE_LFO1_RATE: return "LFO 1 Rate";
    case OUTPUT_TYPE_LFO1_DEPTH: return "LFO 1 Depth";
    case OUTPUT_TYPE_LFO2_RATE: return "LFO 2 Rate";
    case OUTPUT_TYPE_LFO2_DEPTH: return "LFO 2 Depth";
    case OUTPUT_TYPE_RTG_RATE: return "RTG Rate";
    case OUTPUT_TYPE_SH_RATE: return "S+H Rate";
    case OUTPUT_TYPE_PITCH_BEND: return "Pitch Bend";
    case OUTPUT_TYPE_TEMPO_NUDGE: return "Tempo Nudge";
    default: return "Unknown";
  }
}

static void scene_inspect_cc_label(char *buf, size_t cap, const device_def_t *device, uint8_t cc) {
  const char *name = device ? assets_get_cc_name(device, cc) : NULL;
  if (name && strcmp(name, "Undefined") != 0) {
    snprintf(buf, cap, "%s", name);
  } else {
    snprintf(buf, cap, "CC %u", (unsigned)cc);
  }
}

static bool scene_inspect_cc_assigned(const device_def_t *device, uint8_t cc) {
  if (!device || cc > 127) return false;
  return assets_get_control_by_cc(device, cc) != NULL;
}

static const char *SCENE_INSPECT_NOTE_NAMES[] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static void scene_inspect_note_name(uint8_t midi_note, char *buf, size_t cap) {
  if (midi_note > 127) midi_note = 127;
  int octave = (midi_note / 12) - 1;
  int note_idx = midi_note % 12;
  snprintf(buf, cap, "%s%d", SCENE_INSPECT_NOTE_NAMES[note_idx], octave);
}

static void append_continuous_assignment(scene_inspect_buf_t *b, const char *prefix,
  const continuous_mapping_t *mapping, uint8_t scene_index) {
  const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);

  if (mapping->output_type == OUTPUT_TYPE_CC) {
    char cc_name[32];
    if (scene_inspect_cc_assigned(device, mapping->cc_number)) {
      scene_inspect_cc_label(cc_name, sizeof(cc_name), device, mapping->cc_number);
      scene_inspect_buf_append(b, "%s: %s %u-%u\n", prefix, cc_name,
        (unsigned)mapping->min_value, (unsigned)mapping->max_value);
    } else {
      scene_inspect_buf_append(b, "%s: Unassigned!\n", prefix);
    }
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    char lo[8];
    char hi[8];
    uint8_t top = mapping->base_note + mapping->note_range;
    if (top > 127) top = 127;
    scene_inspect_note_name(mapping->base_note, lo, sizeof(lo));
    scene_inspect_note_name(top, hi, sizeof(hi));
    scene_inspect_buf_append(b, "%s: %s-%s\n", prefix, lo, hi);
    return;
  }

  const char *out = scene_inspect_output_type_name(mapping->output_type);
  scene_inspect_buf_append(b, "%s: %s\n", prefix, out);
}

static const char *inspect_lfo_target_label(lfo_target_t target) {
  switch (target) {
    case LFO_TARGET_LFO1: return "LFO 1";
    case LFO_TARGET_LFO2: return "LFO 2";
    default: return "LFO 1 + LFO 2";
  }
}

static bool mapping_uses_lfo_target(output_type_t type) {
  return type == OUTPUT_TYPE_LFO_RATE || type == OUTPUT_TYPE_LFO_DEPTH;
}

static const char* touchwheel_nudge_return_inspect_label(uint8_t speed) {
  switch (speed) {
    case TOUCHWHEEL_NUDGE_RETURN_FAST: return "Fast (200ms)";
    case TOUCHWHEEL_NUDGE_RETURN_MEDIUM: return "Medium (500ms)";
    case TOUCHWHEEL_NUDGE_RETURN_SLOW: return "Slow (1s)";
    default: return "Instant";
  }
}

static void append_continuous_mapping_block(scene_inspect_buf_t *b, const char *label,
  const continuous_mapping_t *mapping, uint8_t scene_index, uint8_t nudge_pct,
  int16_t nudge_return) {
  if (!mapping || !mapping->enabled) return;

  if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
    scene_inspect_buf_append(b, "%s: Tempo\n", label);
    scene_inspect_buf_append(b, "Nudge %u%%\n", (unsigned)nudge_pct);
    if (nudge_return >= 0) {
      scene_inspect_buf_append(b, "Return: %s\n\n",
        touchwheel_nudge_return_inspect_label((uint8_t)nudge_return));
    } else {
      scene_inspect_buf_append(b, "\n");
    }
    return;
  }

  if (mapping_uses_lfo_target(mapping->output_type)) {
    const char *kind = (mapping->output_type == OUTPUT_TYPE_LFO_RATE) ? "LFO Rate" : "LFO Depth";
    scene_inspect_buf_append(b, "%s: %s\n", label, kind);
    scene_inspect_buf_append(b, "%s\n\n", inspect_lfo_target_label(mapping->lfo_target));
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_LFO1_RATE ||
      mapping->output_type == OUTPUT_TYPE_LFO1_DEPTH) {
    const char *kind = (mapping->output_type == OUTPUT_TYPE_LFO1_RATE) ? "LFO 1 Rate" : "LFO 1 Depth";
    scene_inspect_buf_append(b, "%s: %s\n", label, kind);
    scene_inspect_buf_append(b, "LFO 2\n\n");
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_LFO2_RATE ||
      mapping->output_type == OUTPUT_TYPE_LFO2_DEPTH) {
    const char *kind = (mapping->output_type == OUTPUT_TYPE_LFO2_RATE) ? "LFO 2 Rate" : "LFO 2 Depth";
    scene_inspect_buf_append(b, "%s: %s\n", label, kind);
    scene_inspect_buf_append(b, "LFO 1\n\n");
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_CC) {
    char cc_name[32];
    const device_def_t *device = (const device_def_t *)scene_get_device(scene_index);
    scene_inspect_buf_append(b, "%s: Control Change\n", label);
    if (scene_inspect_cc_assigned(device, mapping->cc_number)) {
      scene_inspect_cc_label(cc_name, sizeof(cc_name), device, mapping->cc_number);
      scene_inspect_buf_append(b, "Set: %s %u-%u\n\n", cc_name,
        (unsigned)mapping->min_value, (unsigned)mapping->max_value);
    } else {
      scene_inspect_buf_append(b, "Set: Unassigned!\n\n");
    }
    return;
  }

  if (mapping->output_type == OUTPUT_TYPE_NOTE) {
    char lo[8];
    char hi[8];
    uint8_t top = mapping->base_note + mapping->note_range;
    if (top > 127) top = 127;
    scene_inspect_note_name(mapping->base_note, lo, sizeof(lo));
    scene_inspect_note_name(top, hi, sizeof(hi));
    scene_inspect_buf_append(b, "%s: Notes\n", label);
    scene_inspect_buf_append(b, "Set: %s-%s\n\n", lo, hi);
    return;
  }

  scene_inspect_buf_append(b, "%s: %s\n\n", label,
    scene_inspect_output_type_name(mapping->output_type));
}

static void append_header(scene_inspect_buf_t *b, const scene_t *scene) {
  const char *name = (scene && scene->name[0]) ? scene->name : "Untitled";
  scene_inspect_buf_append(b, "%s\n\n", name);
}

static void append_overview(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  scene_inspect_buf_append(b, "%u BPM, %u/%u\n",
    (unsigned)scene->bpm,
    (unsigned)scene->time_signature.numerator,
    (unsigned)scene->time_signature.denominator);

  if (scene->send_clock) {
    scene_inspect_buf_append(b, "Clock: %s\n",
      clock_source_name(scene->clock_source));
  }

  const char *mod_name = scene_get_ui_module(scene_index);
  const char *screen_title = ui_get_module_title(mod_name);
  scene_inspect_buf_append(b, "Screen: %s\n", screen_title);

  scene_inspect_buf_append(b, "Divider: %s\n",
    beat_divider_name(scene->beat_divider));

  scene_inspect_buf_append(b, "Transport: %s\n",
    scene->use_transport ? "Yes" : "No");

  const char *global_slug = device_config_get_pedal_slug();
  const char *effective_slug = scene_get_effective_device_slug(scene_index);
  bool device_differs = effective_slug && global_slug &&
    strcmp(effective_slug, global_slug) != 0;
  bool has_midi_override = scene->midi_channel != 0;
  bool has_trs_override = scene->trs_type != 0;
  bool has_note_override = scene->note_channel != 0;

  if (device_differs || has_midi_override || has_trs_override || has_note_override) {
    if (device_differs) {
      const manifest_device_t *mdev = assets_get_manifest_device(effective_slug);
      const char *device_name = (mdev && mdev->name[0]) ? mdev->name : effective_slug;
      scene_inspect_buf_append(b, "Pedal: %s\n", device_name);
    }

    if (has_midi_override || has_trs_override) {
      if (has_midi_override && has_trs_override) {
        scene_inspect_buf_append(b, "MIDI Ch %u, TRS %s\n",
          (unsigned)scene->midi_channel, trs_type_name(scene->trs_type));
      } else if (has_midi_override) {
        scene_inspect_buf_append(b, "MIDI Ch %u\n", (unsigned)scene->midi_channel);
      } else {
        scene_inspect_buf_append(b, "TRS %s\n", trs_type_name(scene->trs_type));
      }
    }

    if (has_note_override) {
      scene_inspect_buf_append(b, "Note Ch %u\n", (unsigned)scene->note_channel);
    }
  }

  if (scene->send_pc_on_load) {
    uint16_t index_base = scene_inspect_device_index_base(scene_index);
    uint8_t pc = scene->program_number;
    if (pc < index_base) pc = index_base;
    int display_preset = (int)pc - (int)index_base + 1;
    scene_inspect_buf_append(b, "Preset: %d\n", display_preset);
  }
}

static const char *inspect_pad_name(uint8_t pad_index) {
  static const char *names[] = {
    "Pad 1", "Pad 2", "Pad 3", "Pad 4", "Pad 5", "Pad 6", "Pad 7", "Pad 8",
    "Omega", "Alpha", "Beta", "Gamma"
  };
  if (pad_index < NUM_TOUCHPADS) return names[pad_index];
  return "?";
}

static void append_touchpads(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  int start_pad = (scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS) ? 0 : TOUCHWHEEL_SIZE;

  bool any_pad = false;
  for (int i = start_pad; i < NUM_TOUCHPADS; i++) {
    const touchpad_mapping_t *map = &scene->touchpads[i];
    if (map->action.type != ACTION_NONE) {
      any_pad = true;
      break;
    }
  }
  if (!any_pad) return;

  char pad_block[768];

  for (int i = start_pad; i < NUM_TOUCHPADS; i++) {
    const touchpad_mapping_t *map = &scene->touchpads[i];
    if (map->action.type == ACTION_NONE) continue;

    if (!action_summary_format_inspect_pad(&map->action, inspect_pad_name((uint8_t)i),
        scene_index, pad_block, sizeof(pad_block), true)) {
      continue;
    }

    scene_inspect_buf_append(b, "%s\n\n", pad_block);
  }
}

static void append_touchwheel(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS) return;

  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    append_continuous_mapping_block(b, "Touchwheel", &scene->touchwheel, scene_index,
      scene->touchwheel_tempo_nudge_pct, scene->touchwheel_tempo_nudge_return);
    return;
  }

  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_LFO_RATE) {
    scene_inspect_buf_append(b, "Touchwheel: LFO Rate\n");
    scene_inspect_buf_append(b, "%s\n\n", inspect_lfo_target_label(scene->touchwheel_lfo_target));
    return;
  }

  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_LFO_DEPTH) {
    scene_inspect_buf_append(b, "Touchwheel: LFO Depth\n");
    scene_inspect_buf_append(b, "%s\n\n", inspect_lfo_target_label(scene->touchwheel_lfo_target));
    return;
  }

  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_AFTERTOUCH) {
    scene_inspect_buf_append(b, "Touchwheel: After Touch\n");
    if (scene->touchwheel_aftertouch_return != TOUCHWHEEL_NUDGE_RETURN_INSTANT) {
      scene_inspect_buf_append(b, "Return: %s\n",
        touchwheel_nudge_return_inspect_label(scene->touchwheel_aftertouch_return));
    }
    scene_inspect_buf_append(b, "\n");
    return;
  }

  action_summary_t summary;
  action_summary_init(&summary);
  action_summary_set_input(&summary, SUMMARY_INPUT_TOUCHWHEEL, true);
  touchwheel_format_summary(scene, &summary);
  append_summary_line(b, &summary);
}

static void append_expression(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  // CV/Gate mode owns the expression jack summary in append_cv().
  if (scene->cv_input_mode == INPUT_MODE_NOTE) return;

  bool connected = expression_is_connected();

  if (scene->expression_mode == EXPRESSION_MODE_NONE ||
      (scene->expression_mode == EXPRESSION_MODE_PEDAL && !scene->expression.enabled)) {
    return;
  }

  switch (scene->expression_mode) {
    case EXPRESSION_MODE_PEDAL:
      append_cv_mapping_block(b, "Expression", connected, &scene->expression, scene_index,
        scene->expression_tempo_nudge_pct);
      return;

    case EXPRESSION_MODE_SUSTAIN:
      if (scene->sustain.type != ACTION_NONE) {
        append_jack_action_block(b, "Sustain Pedal", connected, &scene->sustain, scene_index);
      } else {
        append_pedal_enabled_block(b, "Sustain Pedal", connected);
      }
      return;

    case EXPRESSION_MODE_SOSTENUTO:
      if (scene->sostenuto.type != ACTION_NONE) {
        append_jack_action_block(b, "Sostenuto Pedal", connected, &scene->sostenuto, scene_index);
      } else {
        append_pedal_enabled_block(b, "Sostenuto Pedal", connected);
      }
      return;

    case EXPRESSION_MODE_SWITCH:
      append_jack_action_block(b, "Foot Switch", connected, &scene->expr_switch, scene_index);
      return;

    case EXPRESSION_MODE_GATE:
      append_pedal_enabled_block(b, "Gate", connected);
      return;

    default:
      return;
  }
}

static void append_cv_audio_inspect(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index, bool cv_connected) {
  uint8_t sens = scene_get_audio_sensitivity(scene_index);
  float gain = 0.25f * powf(256.0f, sens / 255.0f);
  uint8_t thresh = scene_get_audio_threshold(scene_index);
  cv_range_t audio_range = scene_get_audio_range(scene_index);
  const char *range_str = (audio_range == CV_RANGE_BIPOLAR_10V) ? "+-10V" : "+-5V";
  uint16_t attack = scene_get_audio_attack(scene_index);
  uint16_t release = scene_get_audio_release(scene_index);

  scene_inspect_buf_append(b, "Audio: %s\n", jack_connection_status(cv_connected));
  scene_inspect_buf_append(b, "Sensitivity/Threshold/Range:\n");

  if (gain >= 10.0f) {
    scene_inspect_buf_append(b, "%.0fx, %u, %s\n", gain, (unsigned)thresh, range_str);
  } else {
    scene_inspect_buf_append(b, "%.1fx, %u, %s\n", gain, (unsigned)thresh, range_str);
  }

  scene_inspect_buf_append(b, "Attack/Release: %ums/%ums\n",
    (unsigned)attack, (unsigned)release);

  if (scene->cv.enabled) {
    append_continuous_assignment(b, "Set", &scene->cv, scene_index);
  }

  scene_inspect_buf_append(b, "\n");
}

static void append_cv(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  bool cv_connected = cv_is_cable_connected();
  bool gate_connected = expression_is_connected();

  if (scene->cv_input_mode == INPUT_MODE_NONE && !scene->cv.enabled) return;

  if (scene->cv_input_mode == INPUT_MODE_NOTE) {
    scene_inspect_buf_append(b, "CV: %s\n", jack_connection_status(cv_connected));
    scene_inspect_buf_append(b, "Gate: %s\n", jack_connection_status(gate_connected));
    scene_inspect_buf_append(b, "Velocity: %s\n\n", cv_velocity_inspect_label(scene));
    return;
  }

  if (scene->cv_input_mode == INPUT_MODE_AUDIO) {
    append_cv_audio_inspect(b, scene, scene_index, cv_connected);
    return;
  }

  if (scene->cv_input_mode == INPUT_MODE_CLOCK_SYNC) {
    scene_inspect_buf_append(b, "CV: %s\n", jack_connection_status(cv_connected));
    scene_inspect_buf_append(b, "Clock sync\n\n");
    return;
  }

  if (scene->cv_input_mode == INPUT_MODE_TRIGGER) {
    scene_inspect_buf_append(b, "CV: %s\n", jack_connection_status(cv_connected));
    scene_inspect_buf_append(b, "Trigger (>= %u%%)\n",
      (unsigned)scene->cv_trigger_threshold);
    if (scene->cv_trigger_debounce_ms == 0)
      scene_inspect_buf_append(b, "Debounce: Immediate\n");
    else
      scene_inspect_buf_append(b, "Debounce: %ums\n",
        (unsigned)scene->cv_trigger_debounce_ms);
    append_inspect_action(b, "Action", &scene->cv_trigger_action, scene_index, true, true);
    return;
  }

  if (scene->cv_input_mode == INPUT_MODE_CV) {
    append_cv_mapping_block(b, "CV", cv_connected, &scene->cv, scene_index,
      scene->cv_tempo_nudge_pct);
    return;
  }

  scene_inspect_buf_append(b, "CV: %s\n\n", jack_connection_status(cv_connected));
}

static void append_buttons(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  bool any = false;
  if (scene->button_left.type != ACTION_NONE) {
    append_inspect_action(b, "Left", &scene->button_left, scene_index, false, true);
    any = true;
  }
  if (scene->button_right.type != ACTION_NONE) {
    append_inspect_action(b, "Right", &scene->button_right, scene_index, false, true);
    any = true;
  }
  if (scene->button_both.type != ACTION_NONE) {
    append_inspect_action(b, "Both", &scene->button_both, scene_index, false, true);
    any = true;
  }
  if (any) scene_inspect_buf_append(b, "\n");
}

static const char *inspect_lfo_waveform_label(lfo_waveform_t wf) {
  switch (wf) {
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

static const char *inspect_lfo_division_note_label(lfo_note_division_t div) {
  switch (div) {
    case LFO_DIVISION_HALF: return "1/2 Note Division";
    case LFO_DIVISION_QUARTER: return "1/4 Note Division";
    case LFO_DIVISION_EIGHTH: return "1/8 Note Division";
    case LFO_DIVISION_SIXTEENTH: return "1/16 Note Division";
    case LFO_DIVISION_32ND: return "1/32 Note Division";
    case LFO_DIVISION_1_BAR: return "1 Bar Division";
    case LFO_DIVISION_2_BARS: return "2 Bar Division";
    case LFO_DIVISION_4_BARS: return "4 Bar Division";
    case LFO_DIVISION_8_BARS: return "8 Bar Division";
    case LFO_DIVISION_12_BARS: return "12 Bar Division";
    case LFO_DIVISION_16_BARS: return "16 Bar Division";
    default: return "?";
  }
}


static void append_lfo_slot(scene_inspect_buf_t *b, uint8_t slot,
  const scene_t *scene, uint8_t scene_index) {
  const lfo_config_t *config = (slot == 0) ? &scene->lfo1_config : &scene->lfo2_config;
  const continuous_mapping_t *mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  const char *label = (slot == 0) ? "LFO 1" : "LFO 2";

  if (!config->enabled) return;

  scene_inspect_buf_append(b, "%s: %s\n", label,
    inspect_lfo_waveform_label(config->waveform));

  if (config->rate_mode == LFO_RATE_MODE_TEMPO) {
    scene_inspect_buf_append(b, "Division: %s\n",
      inspect_lfo_division_note_label(config->division));
  } else {
    float hz = (float)config->rate_hz_x100 / 100.0f;
    scene_inspect_buf_append(b, "Time: %.1fHz\n", hz);
  }

  if (mapping->enabled) {
    if (mapping->output_type == OUTPUT_TYPE_TEMPO_NUDGE) {
      uint8_t pct = (slot == 0) ? scene->lfo1_tempo_nudge_pct : scene->lfo2_tempo_nudge_pct;
      scene_inspect_buf_append(b, "Set: Tempo\n");
      scene_inspect_buf_append(b, "Nudge %u%%\n", (unsigned)pct);
    } else {
      append_continuous_assignment(b, "Set", mapping, scene_index);
    }
  }

  scene_inspect_buf_append(b, "\n");
}

static const char *inspect_rtg_generator_label(rtg_generator_t gen) {
  switch (gen) {
    case RTG_GEN_SHEPARD: return "Shepard Tone";
    default: return "Random";
  }
}

static void append_rtg(scene_inspect_buf_t *b, const scene_t *scene) {
  if (!scene || !scene->rtg_config.enabled) return;

  const rtg_config_t *rc = &scene->rtg_config;
  scene_inspect_buf_append(b, "RTG: %s\n", inspect_rtg_generator_label(rc->generator));

  if (rc->rate_mode == RTG_RATE_MODE_FREE) {
    float hz = (float)rc->rate_hz_x100 / 100.0f;
    scene_inspect_buf_append(b, "Rate: %.1f Hz\n", hz);
  } else {
    float mult = (float)rc->sync_mult_x1000 / 1000.0f;
    scene_inspect_buf_append(b, "Rate: %.1fx BPM\n", mult);
  }

  scene_inspect_buf_append(b, "Glide: %s\n\n", rc->glide ? "On" : "Off");
}

static const char *inspect_sh_mode_label(sample_hold_mode_t mode) {
  switch (mode) {
    case SAMPLE_HOLD_MODE_STEP: return "Step";
    default: return "Continuous";
  }
}

static void append_sample_hold(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || !scene->sample_hold_config.enabled) return;

  const sample_hold_config_t *config = &scene->sample_hold_config;
  const continuous_mapping_t *mapping = &scene->sample_hold;

  scene_inspect_buf_append(b, "S+H: %s\n", inspect_sh_mode_label(config->mode));

  if (config->rate_mode == SAMPLE_HOLD_RATE_MODE_FREE) {
    float hz = (float)config->rate_hz_x100 / 100.0f;
    scene_inspect_buf_append(b, "Timing: %.1f Hz\n", hz);
  } else {
    float mult = (float)config->sync_mult_x1000 / 1000.0f;
    scene_inspect_buf_append(b, "Timing: %.1fx BPM\n", mult);
  }

  if (mapping->enabled) {
    append_continuous_assignment(b, "Set", mapping, scene_index);
  }

  scene_inspect_buf_append(b, "\n");
}

static void append_cc_triggers(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || !midi_control_is_enabled()) return;

  for (int i = 0; i < NUM_CC_TRIGGERS; i++) {
    const cc_trigger_slot_t *slot = &scene->cc_triggers[i];
    if (slot->action.type == ACTION_NONE) continue;

    char label[24];
    snprintf(label, sizeof(label), "Trigger %d (CC %u)", i + 1,
      (unsigned)slot->cc_number);
    append_inspect_action(b, label, &slot->action, scene_index, true, true);
  }
}

static void append_note_track(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || !scene->note_track.enabled) return;

  scene_inspect_buf_append(b, "Note Track: %s\n",
    scene_inspect_output_type_name(scene->note_track.output_type));

  if (scene->note_track.output_type == OUTPUT_TYPE_CC) {
    append_continuous_assignment(b, "Set", &scene->note_track, scene_index);
  }

  scene_inspect_buf_append(b, "\n");
}

static void append_on_load(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  for (int i = 0; i < scene->num_on_load_actions; i++) {
    const action_t *action = &scene->on_load[i];
    if (action->type == ACTION_NONE) continue;
    char label[16];
    snprintf(label, sizeof(label), "On-Load %d", i + 1);
    append_inspect_action(b, label, action, scene_index, true, false);
  }
}

static void append_on_play(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || !scene->use_transport) return;

  for (int i = 0; i < scene->num_on_play_actions; i++) {
    const action_t *action = &scene->on_play[i];
    if (action->type == ACTION_NONE) continue;
    char label[16];
    snprintf(label, sizeof(label), "On-Play %d", i + 1);
    append_inspect_action(b, label, action, scene_index, true, true);
  }
}

static void append_pending(scene_inspect_buf_t *b) {
  if (!scene_has_pending_change()) return;
  scene_inspect_buf_append(b, "\nPending scene %u\n",
    (unsigned)(scene_get_pending_index() + 1));
}

bool scene_inspect_build(const scene_t *scene, uint8_t scene_index, char *buf,
  size_t cap) {
  if (!buf || cap == 0) return false;

  if (!scene) {
    scene = scene_get_current();
    if (!scene) {
      snprintf(buf, cap, "(no scene)");
      return false;
    }
  }

  scene_inspect_buf_t b;
  scene_inspect_buf_init(&b, buf, cap);

  append_header(&b, scene);
  append_overview(&b, scene, scene_index);
  scene_inspect_buf_append(&b, "\n");

  append_touchpads(&b, scene, scene_index);
  append_touchwheel(&b, scene, scene_index);
  append_expression(&b, scene, scene_index);
  append_cv(&b, scene, scene_index);
  append_continuous_mapping_block(&b, "Proximity", &scene->proximity, scene_index,
    scene->proximity_tempo_nudge_pct, -1);
  append_continuous_mapping_block(&b, "Ambient Light", &scene->als, scene_index,
    scene->als_tempo_nudge_pct, -1);
  append_buttons(&b, scene, scene_index);
  append_lfo_slot(&b, 0, scene, scene_index);
  append_lfo_slot(&b, 1, scene, scene_index);
  append_inspect_action(&b, "Bump", &scene->bump, scene_index, true, true);
  append_on_load(&b, scene, scene_index);
  append_on_play(&b, scene, scene_index);
  append_sample_hold(&b, scene, scene_index);
  append_continuous_mapping_block(&b, "Tilt X", &scene->tilt_x, scene_index,
    scene->tilt_x_tempo_nudge_pct, -1);
  append_continuous_mapping_block(&b, "Tilt Y", &scene->tilt_y, scene_index,
    scene->tilt_y_tempo_nudge_pct, -1);
  append_rtg(&b, scene);
  append_cc_triggers(&b, scene, scene_index);
  append_note_track(&b, scene, scene_index);
  append_pending(&b);

  append_truncation_marker(&b);
  return !b.truncated;
}
