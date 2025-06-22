idf_component_register(
  SRCS
    "bump.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings midi
  PRIV_REQUIRES main
) 