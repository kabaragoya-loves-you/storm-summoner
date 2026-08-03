idf_component_register(
  SRCS "debug_uart.c"
  INCLUDE_DIRS "include"
  REQUIRES driver freertos log console app_settings
)
