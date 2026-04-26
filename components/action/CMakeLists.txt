idf_component_register(
  SRCS "action.c" "action_summary.c"
  INCLUDE_DIRS "include"
  REQUIRES "midi" "device_config" "scene" "transport" "tempo" "screensaver" "lfo" "rtg" "sample_hold" "assets_manager" "event_bus"
  PRIV_REQUIRES "ui"
)

