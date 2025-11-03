idf_component_register(
  SRCS "dac.c" "dac_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "i2c_common" "app_settings" "adc_manager" "driver" "console"
  PRIV_REQUIRES "main"
)
