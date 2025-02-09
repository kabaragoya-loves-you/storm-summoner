idf_component_register(
  SRCS
    "drv2605.c" "drv2605_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common
)