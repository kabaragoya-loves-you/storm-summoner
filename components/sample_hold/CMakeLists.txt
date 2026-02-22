idf_component_register(
  SRCS "sample_hold.c" "sample_hold_console.c"
  INCLUDE_DIRS "include"
  REQUIRES lfsr esp_timer console
  PRIV_REQUIRES event_bus tempo transport
)
