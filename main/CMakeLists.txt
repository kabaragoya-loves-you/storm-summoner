idf_component_register (
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES
    cv
    display
    drv2605
    expression
    flicker
    i2c_common
    log
    stars
    touch
    vcnl4040
)
