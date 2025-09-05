idf_component_register(
  SRCS
    "screensaver.c"
    "screensaver_event_handler.c"
  INCLUDE_DIRS "include"
  REQUIRES freertos elite stars ui log nvs_flash event_bus
)