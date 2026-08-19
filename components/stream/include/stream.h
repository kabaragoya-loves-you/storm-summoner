#ifndef STREAM_H
#define STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

typedef struct scene_t scene_t;

// Stream action targets. Combo entries (tilt_both, lfo_both) expand to
// two leaf targets at dispatch. JSON strings match the labels below
// except combos: "tilt_both" / "lfo_both".
typedef enum {
  STREAM_TARGET_EXPRESSION = 0,
  STREAM_TARGET_CV,
  STREAM_TARGET_PROXIMITY,
  STREAM_TARGET_ALS,
  STREAM_TARGET_NOTE_TRACK,
  STREAM_TARGET_TILT_X,
  STREAM_TARGET_TILT_Y,
  STREAM_TARGET_TILT_BOTH,
  STREAM_TARGET_LFO1,
  STREAM_TARGET_LFO2,
  STREAM_TARGET_LFO_BOTH,
  STREAM_TARGET_SAMPLE_HOLD,
  STREAM_TARGET_RTG,
  STREAM_TARGET_COUNT
} stream_target_t;

#define STREAM_TARGET_DEFAULT STREAM_TARGET_LFO_BOTH

// Stream Hold press mode (params.stream.hold_mode). 0 is the default so
// existing zero-init / omitted JSON stays Activate.
#define STREAM_HOLD_ACTIVATE 0
#define STREAM_HOLD_CUT 1

esp_err_t stream_init(void);

stream_target_t stream_target_from_string(const char* str);
const char* stream_target_to_string(stream_target_t target);
const char* stream_target_display_name(stream_target_t target);

// True when the scene Mode for this target is not Disabled. Combo
// targets are configured if either leaf is.
bool stream_target_configured(const scene_t* scene, stream_target_t target);

// Runtime active state. Combo targets are active if any configured leaf
// is active. Gate-backed sources (expression/cv/proximity/als/note_track)
// consult a per-source flag; engine-backed sources (tilt/lfo/rtg/s+h)
// consult hardware or engine running state.
bool stream_is_active(stream_target_t target);
void stream_set_active(stream_target_t target, bool active);
void stream_toggle(stream_target_t target);

// Hold Cut snapshot: bit 0 = first leaf (or the single target), bit 1 =
// second leaf on tilt_both / lfo_both. Unconfigured leaves are 0.
uint8_t stream_snapshot_active(stream_target_t target);
void stream_restore_active(stream_target_t target, uint8_t mask);

// Apply continuous_mapping start_mode (Running / Paused / Transport) for
// gate-backed sources and tilt. LFO/RTG/S+H keep their own start_mode.
void stream_apply_start_modes(void);
void stream_apply_start_modes_for(const scene_t* scene);

#endif
