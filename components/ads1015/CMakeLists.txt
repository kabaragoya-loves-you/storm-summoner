idf_component_register(
  SRCS "ads1015.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "i2c_common"
  PRIV_REQUIRES "main"
)
