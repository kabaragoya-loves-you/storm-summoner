idf_component_register(
  SRCS
    "tempo.c"
    "tempo_event_handler.c"
  INCLUDE_DIRS "include"
  REQUIRES log app_settings event_bus transport midi freertos esp_timer
  PRIV_REQUIRES main
)
