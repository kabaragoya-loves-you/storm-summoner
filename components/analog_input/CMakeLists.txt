idf_component_register(
  SRCS
    "analog_input.c"
  INCLUDE_DIRS "include"
  REQUIRES log esp_adc driver adc2 cv
  PRIV_REQUIRES main
)