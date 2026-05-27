#include "scene_inspect.h"
#include "action_summary.h"
#include "assets_manager.h"
#include "device_config.h"
#include "rtg.h"
#include "lfo.h"
#include "sample_hold.h"
#include "input_mode.h"
#include "ui.h"
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
  if (line[0] != '\0') scene_inspect_buf_append(b, "%s\n", line);
}

static void append_action_line(scene_inspect_buf_t *b, const char *label,
  const action_t *action, uint8_t scene_index) {
  if (!action || action->type == ACTION_NONE || !label) return;

  action_summary_t summary;
  action_summary_init(&summary);
  action_format_summary(action, &summary, scene_index);

  if (summary.has_param && summary.has_value) {
    scene_inspect_buf_append(b, "%s: %s %s %s\n", label,
      summary.type_name, summary.param_name, summary.param_value);
  } else if (summary.has_param) {
    scene_inspect_buf_append(b, "%s: %s %s\n", label,
      summary.type_name, summary.param_name);
  } else if (summary.type_name[0] != '\0') {
    scene_inspect_buf_append(b, "%s: %s\n", label, summary.type_name);
  }
}

static void append_continuous_line(scene_inspect_buf_t *b, const char *label,
  const continuous_mapping_t *mapping, uint8_t scene_index) {
  if (!mapping || !mapping->enabled) return;

  action_summary_t summary;
  action_summary_init(&summary);
  snprintf(summary.input_name, sizeof(summary.input_name), "%s", label);
  continuous_format_summary(mapping, &summary, scene_index);
  if (strcmp(summary.type_name, "Disabled") == 0) return;
  append_summary_line(b, &summary);
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

static void append_touchpads(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  int start_pad = (scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS) ? 0 : TOUCHWHEEL_SIZE;
  bool is_pads_mode = (scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS);

  for (int i = start_pad; i < NUM_TOUCHPADS; i++) {
    const touchpad_mapping_t *map = &scene->touchpads[i];
    if (!map->enabled || map->action.type == ACTION_NONE) continue;

    action_summary_t summary;
    action_summary_init(&summary);
    action_summary_set_input(&summary, (summary_input_t)(SUMMARY_INPUT_PAD_0 + i),
      is_pads_mode);
    action_format_summary(&map->action, &summary, scene_index);
    append_summary_line(b, &summary);
  }
}

static void append_touchwheel(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || scene->touchwheel_mode == TOUCHWHEEL_MODE_PADS) return;

  if (scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS) {
    if (!scene->touchwheel.enabled) return;
    append_continuous_line(b, "Touchwheel", &scene->touchwheel, scene_index);
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

  if (scene->expression_mode == EXPRESSION_MODE_PEDAL) {
    if (scene->expression.enabled)
      append_continuous_line(b, "Expression", &scene->expression, scene_index);
    return;
  }

  const char *mode = NULL;
  switch (scene->expression_mode) {
    case EXPRESSION_MODE_SUSTAIN: mode = "Sustain jack"; break;
    case EXPRESSION_MODE_SOSTENUTO: mode = "Sostenuto jack"; break;
    case EXPRESSION_MODE_SWITCH: mode = "Switch jack"; break;
    case EXPRESSION_MODE_GATE: mode = "Gate jack"; break;
    default: break;
  }
  if (!mode) return;

  bool has_action = false;
  if (scene->expression_mode == EXPRESSION_MODE_SUSTAIN &&
      scene->sustain.type != ACTION_NONE) {
    append_action_line(b, mode, &scene->sustain, scene_index);
    has_action = true;
  } else if (scene->expression_mode == EXPRESSION_MODE_SOSTENUTO &&
      scene->sostenuto.type != ACTION_NONE) {
    append_action_line(b, mode, &scene->sostenuto, scene_index);
    has_action = true;
  } else if (scene->expression_mode == EXPRESSION_MODE_SWITCH &&
      scene->expr_switch.type != ACTION_NONE) {
    append_action_line(b, mode, &scene->expr_switch, scene_index);
    has_action = true;
  }

  if (!has_action) scene_inspect_buf_append(b, "%s\n", mode);
}

static void append_cv(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;
  if (scene->cv_input_mode == INPUT_MODE_NONE && !scene->cv.enabled) return;

  if (scene->cv.enabled) {
    append_continuous_line(b, "Control Voltage", &scene->cv, scene_index);
    return;
  }

  const char *cv_mode = "CV";
  switch (scene->cv_input_mode) {
    case INPUT_MODE_CV: cv_mode = "CV"; break;
    case INPUT_MODE_CLOCK_SYNC: cv_mode = "Clock sync"; break;
    case INPUT_MODE_AUDIO: cv_mode = "Audio"; break;
    case INPUT_MODE_NOTE: cv_mode = "Note"; break;
    default: return;
  }
  scene_inspect_buf_append(b, "Control Voltage: %s\n", cv_mode);
}

static void append_buttons(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  append_action_line(b, "Left Button", &scene->button_left, scene_index);
  append_action_line(b, "Right Button", &scene->button_right, scene_index);
  append_action_line(b, "Both Buttons", &scene->button_both, scene_index);
}

static void append_lfo_slot(scene_inspect_buf_t *b, uint8_t slot,
  const scene_t *scene, uint8_t scene_index) {
  const lfo_config_t *config = (slot == 0) ? &scene->lfo1_config : &scene->lfo2_config;
  const continuous_mapping_t *mapping = (slot == 0) ? &scene->lfo1 : &scene->lfo2;
  if (!config->enabled && !mapping->enabled) return;

  action_summary_t summary;
  action_summary_init(&summary);
  action_summary_set_input(&summary,
    slot == 0 ? SUMMARY_INPUT_LFO1 : SUMMARY_INPUT_LFO2, true);
  lfo_format_summary(slot, scene, &summary, scene_index);
  if (strcmp(summary.type_name, "Disabled") == 0) return;
  append_summary_line(b, &summary);
}

static void append_rtg(scene_inspect_buf_t *b, const scene_t *scene) {
  if (!scene || !scene->rtg_config.enabled) return;

  const rtg_config_t *rc = &scene->rtg_config;
  if (rc->rate_mode == RTG_RATE_MODE_FREE) {
    float hz = (float)rc->rate_hz_x100 / 100.0f;
    scene_inspect_buf_append(b, "RTG: %s %.1f Hz\n",
      rtg_generator_to_string(rc->generator), hz);
  } else {
    float mult = (float)rc->sync_mult_x1000 / 1000.0f;
    scene_inspect_buf_append(b, "RTG: %s %.2fx BPM\n",
      rtg_generator_to_string(rc->generator), mult);
  }
}

static void append_sample_hold(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene || !scene->sample_hold_config.enabled) return;

  action_summary_t summary;
  action_summary_init(&summary);
  action_summary_set_input(&summary, SUMMARY_INPUT_SAMPLE_HOLD, true);
  sample_hold_format_summary(scene, &summary, scene_index);
  if (strcmp(summary.type_name, "Disabled") == 0) return;
  append_summary_line(b, &summary);
}

static void append_on_load(scene_inspect_buf_t *b, const scene_t *scene,
  uint8_t scene_index) {
  if (!scene) return;

  for (int i = 0; i < scene->num_on_load_actions; i++) {
    const action_t *action = &scene->on_load[i];
    if (action->type == ACTION_NONE) continue;
    char label[16];
    snprintf(label, sizeof(label), "On-Load %d", i + 1);
    append_action_line(b, label, action, scene_index);
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
    append_action_line(b, label, action, scene_index);
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
  append_continuous_line(&b, "Proximity", &scene->proximity, scene_index);
  append_continuous_line(&b, "Ambient Light", &scene->als, scene_index);
  append_buttons(&b, scene, scene_index);
  append_lfo_slot(&b, 0, scene, scene_index);
  append_lfo_slot(&b, 1, scene, scene_index);
  append_action_line(&b, "Bump", &scene->bump, scene_index);
  append_on_load(&b, scene, scene_index);
  append_on_play(&b, scene, scene_index);
  append_sample_hold(&b, scene, scene_index);
  append_continuous_line(&b, "Tilt X", &scene->tilt_x, scene_index);
  append_continuous_line(&b, "Tilt Y", &scene->tilt_y, scene_index);
  append_rtg(&b, scene);
  append_continuous_line(&b, "Note Track", &scene->note_track, scene_index);
  append_pending(&b);

  append_truncation_marker(&b);
  return !b.truncated;
}
