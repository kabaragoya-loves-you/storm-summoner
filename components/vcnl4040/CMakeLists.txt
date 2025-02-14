idf_component_register(
  SRCS
    "vcnl4040.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common
)