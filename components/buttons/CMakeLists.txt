idf_component_register(
  SRCS "buttons.c"
  INCLUDE_DIRS "include"
  REQUIRES "driver" "freertos" "event_bus" "screensaver" "app_settings"
  PRIV_REQUIRES main
)

