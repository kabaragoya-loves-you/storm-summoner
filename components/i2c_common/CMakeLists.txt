idf_component_register(
  SRCS
    "i2c_common.c"
    "i2c_common_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver console
  PRIV_REQUIRES main
)