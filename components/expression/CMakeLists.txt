idf_component_register(
  SRCS
    "expression.c"
  INCLUDE_DIRS "include"
  REQUIRES log esp_adc adc2 midi
  PRIV_REQUIRES main
)