idf_component_register(
  SRCS
    "flicker.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver esp_hw_support
)