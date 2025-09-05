idf_component_register(
  SRCS
    "tempo.c"
    "tempo_event_handler.c"
  INCLUDE_DIRS "include"
  REQUIRES log app_settings event_bus cv analog_input
  PRIV_REQUIRES main
)
