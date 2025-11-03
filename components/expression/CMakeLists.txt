idf_component_register(
  SRCS "expression.c" "expression_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "adc_manager" "event_bus" "app_settings" "input_manager" "log" "driver" "console"
  PRIV_REQUIRES "main"
)