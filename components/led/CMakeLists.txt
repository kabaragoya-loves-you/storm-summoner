idf_component_register(
  SRCS
    "led.c"
  INCLUDE_DIRS "include"
  REQUIRES log driver esp_hw_support
  PRIV_REQUIRES main
)