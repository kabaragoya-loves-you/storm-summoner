idf_component_register(
  SRCS
    "bump.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings event_bus
  PRIV_REQUIRES main
) 