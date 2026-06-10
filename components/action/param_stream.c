#include "param_stream.h"
#include "action.h"
#include "scene.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "param_stream";

static bool mapping_is_cc_active(const continuous_mapping_t* mapping) {
  return mapping && mapping->enabled && mapping->output_type == OUTPUT_TYPE_CC;
}

static const char* const s_target_strings[] = {
  "touchwheel",
  "expression",
  "cv",
  "proximity",
  "als",
  "tilt_x",
  "tilt_y",
  "note_track",
  "lfo1",
  "lfo2",
};

static const char* const s_target_labels[] = {
  "Touchwheel",
  "Expression",
  "CV",
  "Proximity",
  "ALS",
  "Tilt X",
  "Tilt Y",
  "Note Track",
  "LFO 1",
  "LFO 2",
};

param_target_t param_target_from_string(const char* str) {
  if (!str || !str[0]) return PARAM_TARGET_TOUCHWHEEL;
  for (int i = 0; i < PARAM_TARGET_COUNT; i++) {
    if (strcmp(str, s_target_strings[i]) == 0) return (param_target_t)i;
  }
  return PARAM_TARGET_TOUCHWHEEL;
}

const char* param_target_to_string(param_target_t target) {
  if (target >= PARAM_TARGET_COUNT) return s_target_strings[0];
  return s_target_strings[target];
}

const char* param_target_display_name(param_target_t target) {
  if (target >= PARAM_TARGET_COUNT) return s_target_labels[0];
  return s_target_labels[target];
}

bool param_target_is_cc_active(const scene_t* scene, param_target_t target) {
  if (!scene || target >= PARAM_TARGET_COUNT) return false;

  switch (target) {
    case PARAM_TARGET_TOUCHWHEEL:
      return scene->touchwheel_mode == TOUCHWHEEL_MODE_CONTINUOUS &&
        mapping_is_cc_active(&scene->touchwheel);
    case PARAM_TARGET_EXPRESSION:
      return scene->expression_mode == EXPRESSION_MODE_PEDAL &&
        mapping_is_cc_active(&scene->expression);
    case PARAM_TARGET_CV:
      return scene->cv_input_mode == INPUT_MODE_CV &&
        mapping_is_cc_active(&scene->cv);
    case PARAM_TARGET_PROXIMITY:
      return mapping_is_cc_active(&scene->proximity);
    case PARAM_TARGET_ALS:
      return mapping_is_cc_active(&scene->als);
    case PARAM_TARGET_TILT_X:
      return mapping_is_cc_active(&scene->tilt_x);
    case PARAM_TARGET_TILT_Y:
      return mapping_is_cc_active(&scene->tilt_y);
    case PARAM_TARGET_NOTE_TRACK:
      return mapping_is_cc_active(&scene->note_track);
    case PARAM_TARGET_LFO1:
      return scene->lfo1_config.enabled && mapping_is_cc_active(&scene->lfo1);
    case PARAM_TARGET_LFO2:
      return scene->lfo2_config.enabled && mapping_is_cc_active(&scene->lfo2);
    default:
      return false;
  }
}

continuous_mapping_t* param_target_get_mapping(scene_t* scene, param_target_t target) {
  if (!scene || target >= PARAM_TARGET_COUNT) return NULL;
  if (!param_target_is_cc_active(scene, target)) return NULL;

  switch (target) {
    case PARAM_TARGET_TOUCHWHEEL: return &scene->touchwheel;
    case PARAM_TARGET_EXPRESSION: return &scene->expression;
    case PARAM_TARGET_CV: return &scene->cv;
    case PARAM_TARGET_PROXIMITY: return &scene->proximity;
    case PARAM_TARGET_ALS: return &scene->als;
    case PARAM_TARGET_TILT_X: return &scene->tilt_x;
    case PARAM_TARGET_TILT_Y: return &scene->tilt_y;
    case PARAM_TARGET_NOTE_TRACK: return &scene->note_track;
    case PARAM_TARGET_LFO1: return &scene->lfo1;
    case PARAM_TARGET_LFO2: return &scene->lfo2;
    default: return NULL;
  }
}

uint8_t param_target_get_cc(const scene_t* scene, param_target_t target) {
  if (!scene || !param_target_is_cc_active(scene, target)) return 0;

  if (target == PARAM_TARGET_TOUCHWHEEL) {
    const continuous_mapping_t* m = &scene->touchwheel;
    if (m->num_cc_numbers > 0 && m->cc_numbers[0] > 0) return m->cc_numbers[0];
    return m->cc_number;
  }

  continuous_mapping_t* mapping =
    param_target_get_mapping((scene_t*)scene, target);
  if (!mapping) return 0;
  if (mapping->num_cc_numbers > 0 && mapping->cc_numbers[0] > 0)
    return mapping->cc_numbers[0];
  return mapping->cc_number;
}

void param_target_set_cc(scene_t* scene, param_target_t target, uint8_t cc) {
  continuous_mapping_t* mapping = param_target_get_mapping(scene, target);
  if (!mapping) return;

  mapping->cc_number = cc;
  mapping->cc_numbers[0] = cc;
  mapping->num_cc_numbers = (cc > 0) ? 1 : 0;
  for (int i = 1; i < MAX_MULTI_CC; i++) mapping->cc_numbers[i] = 0;
}

uint8_t param_target_get_value(const scene_t* scene, param_target_t target) {
  if (!scene || !param_target_is_cc_active(scene, target)) return 0;
  if (target == PARAM_TARGET_TOUCHWHEEL) return scene_get_touchwheel_value();

  continuous_mapping_t* mapping =
    param_target_get_mapping((scene_t*)scene, target);
  return mapping ? mapping->last_value : 0;
}

void param_target_set_value(scene_t* scene, param_target_t target, uint8_t value) {
  if (!scene || !param_target_is_cc_active(scene, target)) return;

  if (target == PARAM_TARGET_TOUCHWHEEL) {
    scene_set_touchwheel_value(value);
    return;
  }

  continuous_mapping_t* mapping = param_target_get_mapping(scene, target);
  if (mapping) mapping->last_value = value;
}

void param_target_apply(scene_t* scene, param_target_t target, uint8_t cc, uint8_t value) {
  if (!scene || !param_target_is_cc_active(scene, target)) {
    ESP_LOGW(TAG, "Param target %s not CC-active", param_target_to_string(target));
    return;
  }
  param_target_set_cc(scene, target, cc);
  param_target_set_value(scene, target, value);
  action_set_cc_value(cc, value);
}

void param_target_capture(const scene_t* scene, param_target_t target,
  uint8_t* out_cc, uint8_t* out_value) {
  if (out_cc) *out_cc = param_target_get_cc(scene, target);
  if (out_value) *out_value = param_target_get_value(scene, target);
}

bool param_target_build_options(const scene_t* scene, param_target_options_t* out) {
  if (!scene || !out) return false;
  memset(out, 0, sizeof(*out));

  size_t pos = 0;
  for (int i = 0; i < PARAM_TARGET_COUNT; i++) {
    param_target_t t = (param_target_t)i;
    if (!param_target_is_cc_active(scene, t)) continue;
    if (out->count >= PARAM_TARGET_COUNT) break;

    out->targets[out->count++] = t;
    if (pos > 0 && pos < sizeof(out->options_str) - 1)
      out->options_str[pos++] = '\n';

    const char* label = param_target_display_name(t);
    int written = snprintf(out->options_str + pos,
      sizeof(out->options_str) - pos, "%s", label);
    if (written <= 0 || (size_t)written >= sizeof(out->options_str) - pos) break;
    pos += (size_t)written;
  }

  return out->count > 0;
}
