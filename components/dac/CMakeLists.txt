idf_component_register(
  SRCS "mcp4725.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "i2c_common" "app_settings"
  PRIV_REQUIRES "main"
)
