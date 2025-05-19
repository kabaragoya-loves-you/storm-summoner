idf_component_register(
  SRCS
    "touch.c"
    "touch_thresholds.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver esp_timer haptic midi
  PRIV_REQUIRES main
)