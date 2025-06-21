idf_component_register(
  SRCS
    "touch2.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings
  PRIV_REQUIRES main
) 