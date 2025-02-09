idf_component_register(
  SRCS
    "touch.c"
    "touch_gestures.c"
    "touch_modes.c"
    "touch_thresholds.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver esp_timer
)