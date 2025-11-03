idf_component_register(
  SRCS "input_manager.c" "input_manager_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "cv" "clock_sync" "switch" "app_settings" "expression" "driver" "log" "console"
  PRIV_REQUIRES "main"
)
