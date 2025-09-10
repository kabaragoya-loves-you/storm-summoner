idf_component_register(
  SRCS "cv.c"
  INCLUDE_DIRS "include"
  REQUIRES "ads1015" "event_bus" "app_settings" "freertos" "log" "switch" "driver"
  PRIV_REQUIRES "main"
)