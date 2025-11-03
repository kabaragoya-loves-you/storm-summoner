idf_component_register(
  SRCS
    "bump.c"
    "bump_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings event_bus console
  PRIV_REQUIRES main
) 