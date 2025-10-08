idf_component_register(
  SRCS "cv.c"
  INCLUDE_DIRS "include"
  REQUIRES "adc_manager" "dac" "event_bus" "app_settings" "freertos" "log" "switch" "driver"
  PRIV_REQUIRES "main"
)