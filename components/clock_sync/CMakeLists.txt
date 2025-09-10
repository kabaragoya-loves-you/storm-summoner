idf_component_register(
  SRCS "clock_sync.c"
  INCLUDE_DIRS "include"
  REQUIRES "switch" "event_bus" "app_settings" "freertos" "log" "esp_timer" "driver"
  PRIV_REQUIRES "main"
)
