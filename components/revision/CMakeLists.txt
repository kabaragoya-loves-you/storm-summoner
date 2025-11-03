idf_component_register(
  SRCS "revision.c" "revision_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "adc_manager" "driver" "freertos" "console"
  PRIV_REQUIRES main
)
