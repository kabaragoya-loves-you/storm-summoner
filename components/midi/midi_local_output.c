#include "midi_local_output.h"

#include "input_manager.h"
#include "scene.h"
#include "midi_cv_scene_handler.h"
#include "midi_expression_scene_handler.h"
#include "midi_als_scene_handler.h"
#include "midi_proximity_scene_handler.h"
#include "midi_lfo_scene_handler.h"
#include "midi_tilt_scene_handler.h"
#include "action_handlers_midi.h"
#include "rtg.h"
#include "tempo.h"
#include "esp_log.h"

#define TAG "MIDI_LOCAL_OUT"

// Default to enabled so initial boot in PERFORMANCE mode produces MIDI.
static bool s_enabled = true;

bool midi_local_output_is_enabled(void) {
  return s_enabled;
}

void midi_local_output_release_all(void) {
  input_manager_release_active_notes();
  scene_touchwheel_cleanup_notes();
  midi_cv_scene_handler_release_notes();
  midi_expression_scene_handler_release_notes();
  midi_als_scene_handler_release_notes();
  midi_proximity_scene_handler_release_notes();
  midi_lfo_scene_handler_release_notes();
  midi_tilt_scene_handler_release_notes();
  action_handlers_midi_release_notes();
  rtg_release_notes();
  ESP_LOGD(TAG, "Released all on-device producer notes");
}

void midi_local_output_silence(void) {
  // Release first while the predicate is still true so NoteOffs flow through
  // any producer-level checks normally.
  midi_local_output_release_all();
  tempo_set_clock_muted(true);
  s_enabled = false;
  ESP_LOGD(TAG, "Local MIDI output silenced");
  // LFO loop disable + save/restore stays in scene_suspend_input /
  // scene_resume_input -- it has to coordinate with scene_apply_deferred_init
  // when a scene change happens during programming mode.
}

void midi_local_output_enable(void) {
  tempo_set_clock_muted(false);
  s_enabled = true;
  ESP_LOGD(TAG, "Local MIDI output enabled");
}
