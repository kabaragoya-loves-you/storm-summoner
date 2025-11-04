idf_component_register(
  SRCS "device_config.c" "device_config_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "app_settings" "assets_manager" "console" "midi"
)

