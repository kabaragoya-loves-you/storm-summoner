#ifndef PARAM_STREAM_H
#define PARAM_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "continuous_mapping.h"

typedef struct scene_t scene_t;

typedef enum {
  PARAM_TARGET_TOUCHWHEEL = 0,
  PARAM_TARGET_EXPRESSION,
  PARAM_TARGET_CV,
  PARAM_TARGET_PROXIMITY,
  PARAM_TARGET_ALS,
  PARAM_TARGET_TILT_X,
  PARAM_TARGET_TILT_Y,
  PARAM_TARGET_NOTE_TRACK,
  PARAM_TARGET_LFO1,
  PARAM_TARGET_LFO2,
  PARAM_TARGET_COUNT
} param_target_t;

#define PARAM_TARGET_OPTIONS_MAX 32

typedef struct {
  param_target_t targets[PARAM_TARGET_COUNT];
  uint8_t count;
  char options_str[256];
} param_target_options_t;

param_target_t param_target_from_string(const char* str);
const char* param_target_to_string(param_target_t target);
const char* param_target_display_name(param_target_t target);

bool param_target_is_cc_active(const scene_t* scene, param_target_t target);
continuous_mapping_t* param_target_get_mapping(scene_t* scene, param_target_t target);

uint8_t param_target_get_cc(const scene_t* scene, param_target_t target);
void param_target_set_cc(scene_t* scene, param_target_t target, uint8_t cc);

uint8_t param_target_get_value(const scene_t* scene, param_target_t target);
void param_target_set_value(scene_t* scene, param_target_t target, uint8_t value);

void param_target_apply(scene_t* scene, param_target_t target, uint8_t cc, uint8_t value);
void param_target_capture(const scene_t* scene, param_target_t target,
  uint8_t* out_cc, uint8_t* out_value);

bool param_target_build_options(const scene_t* scene, param_target_options_t* out);

#endif
