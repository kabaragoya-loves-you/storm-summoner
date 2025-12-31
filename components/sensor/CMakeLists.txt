idf_component_register(
  SRCS
    "sensor.c"
    "sensor_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver i2c_common event_bus app_settings console scene ui
  PRIV_REQUIRES main
)