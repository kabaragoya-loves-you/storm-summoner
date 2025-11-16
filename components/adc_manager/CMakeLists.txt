idf_component_register(
  SRCS "adc_manager_console.c" "adc_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_adc driver freertos console
  PRIV_REQUIRES main
)

