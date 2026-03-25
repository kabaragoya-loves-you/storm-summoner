idf_component_register(
  SRCS "version.c" "version_console.c"
  INCLUDE_DIRS "include" "${CMAKE_BINARY_DIR}"
  REQUIRES "esp_hw_support" "console" "app_settings"
)

