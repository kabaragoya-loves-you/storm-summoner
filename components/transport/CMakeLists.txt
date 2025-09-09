idf_component_register(
  SRCS "transport.c"
  INCLUDE_DIRS "include"
  REQUIRES "event_bus" "log" "freertos"
)
