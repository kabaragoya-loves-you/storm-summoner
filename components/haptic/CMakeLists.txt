idf_component_register(
  SRCS
    "haptic.c" "haptic_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common
)