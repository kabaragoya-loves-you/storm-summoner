idf_component_register(
  SRCS
    "adc.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings midi
  PRIV_REQUIRES main
) 