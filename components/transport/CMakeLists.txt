idf_component_register(
  SRCS "transport.c" "transport_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "event_bus" "log" "freertos" "console"
)
