idf_component_register(
  SRCS "buttons.c" "buttons_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "driver" "freertos" "event_bus" "screensaver" "app_settings" "console"
  PRIV_REQUIRES main
)

