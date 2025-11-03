idf_component_register(
  SRCS "cv.c" "cv_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "adc_manager" "dac" "event_bus" "app_settings" "freertos" "log" "switch" "driver" "console"
  PRIV_REQUIRES "main"
)