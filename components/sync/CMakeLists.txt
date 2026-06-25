idf_component_register(
  SRCS "sync.c" "sync_console.c"
  INCLUDE_DIRS "include"
  REQUIRES event_bus transport tempo clock_sync scene freertos log esp_timer console
  PRIV_REQUIRES main
)
