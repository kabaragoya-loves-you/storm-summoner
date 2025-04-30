idf_component_register(
  SRCS
    "sensor.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common midi
  PRIV_REQUIRES main
)