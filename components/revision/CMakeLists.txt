idf_component_register(
  SRCS "revision.c"
  INCLUDE_DIRS "include"
  REQUIRES adc_manager driver freertos
  PRIV_REQUIRES main
)
