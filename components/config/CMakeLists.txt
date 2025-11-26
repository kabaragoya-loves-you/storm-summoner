idf_component_register(
  SRCS "config.c" "config_console.c"
  INCLUDE_DIRS "include"
  REQUIRES console scene device_config app_settings
)

