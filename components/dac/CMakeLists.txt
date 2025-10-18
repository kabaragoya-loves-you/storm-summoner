idf_component_register(
  SRCS "dac.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "i2c_common" "app_settings" "adc_manager" "driver"
  PRIV_REQUIRES "main"
)
