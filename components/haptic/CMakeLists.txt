idf_component_register(
  SRCS
    "haptic.c" "haptic_manager.c" "haptic_event_handler.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common event_bus
  PRIV_REQUIRES main
)