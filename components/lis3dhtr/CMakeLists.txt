idf_component_register(
  SRCS
    "lis3dhtr.c"
    "bump.c"
    "tilt.c"
    "bump_console.c"
    "tilt_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common app_settings event_bus console
  PRIV_REQUIRES main
)
