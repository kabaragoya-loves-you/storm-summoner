idf_component_register(
  SRCS "revision.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_adc driver freertos
  PRIV_REQUIRES main
)
