idf_component_register(
  SRCS
    "haptic.c" "haptic_manager.c" "haptic_event_handler.c" "haptic_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common event_bus console
  PRIV_REQUIRES main
)