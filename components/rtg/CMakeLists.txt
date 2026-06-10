idf_component_register(
  SRCS "rtg.c"
  INCLUDE_DIRS "include"
  REQUIRES midi esp_timer lfsr
  PRIV_REQUIRES scene event_bus tempo transport lfo
)
