idf_component_register(
  SRCS "expression.c"
  INCLUDE_DIRS "include"
  REQUIRES "ads1015" "event_bus" "app_settings" "log" "driver"
  PRIV_REQUIRES "main"
)