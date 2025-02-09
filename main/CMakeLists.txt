idf_component_register (
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES display i2c_common stars touch drv2605 log
)
