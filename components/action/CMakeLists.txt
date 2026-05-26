idf_component_register(
  SRCS "action.c" "action_scheduler.c" "action_morph.c" "action_boomerang.c"
       "action_punch_in.c" "action_clock_burst.c" "action_note_hold.c"
       "action_strings.c"
       "action_validation.c" "action_handlers_midi.c" "action_handlers_scene.c"
       "action_handlers_modulation.c" "action_summary.c"
  INCLUDE_DIRS "include"
  REQUIRES "midi" "device_config" "scene" "transport" "tempo" "lfo" "rtg" "sample_hold" "assets_manager" "event_bus" "curve"
  PRIV_REQUIRES "ui"
)

