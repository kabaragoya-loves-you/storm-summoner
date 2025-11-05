idf_component_register(
  SRCS
    "led.c" "led_event_handler.c" "led_console.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver esp_hw_support event_bus app_settings console sensor
  PRIV_REQUIRES main
)