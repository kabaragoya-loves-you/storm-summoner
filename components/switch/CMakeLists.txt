idf_component_register(
  SRCS "switch.c" "switch_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "i2c_common" "console" "revision"
  PRIV_REQUIRES main
)
