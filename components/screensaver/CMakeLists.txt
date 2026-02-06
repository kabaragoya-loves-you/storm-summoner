idf_component_register(
  SRCS
    "screensaver.c"
    "screensaver_event_handler.c"
    "screensaver_console.c"
  INCLUDE_DIRS "include"
  REQUIRES freertos elite stars plasma ui log nvs_flash event_bus console
)