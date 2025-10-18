idf_component_register(
  SRCS "input_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES "cv" "clock_sync" "switch" "app_settings" "driver" "log"
  PRIV_REQUIRES "main"
)
