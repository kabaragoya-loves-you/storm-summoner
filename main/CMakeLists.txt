idf_component_register (
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES
    display
    drv2605
    flicker
    i2c_common
    log
    stars
    touch
)
