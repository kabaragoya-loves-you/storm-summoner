idf_component_register(
  SRCS "action.c"
  INCLUDE_DIRS "include"
  REQUIRES "midi" "device_config" "scene" "transport" "tempo" "screensaver" "lfo" "rtg" "sample_hold"
  PRIV_REQUIRES "ui"
)

