idf_component_register(
  SRCS
    "screensaver.c"
  INCLUDE_DIRS "include"
  REQUIRES freertos elite stars ui log nvs_flash
)