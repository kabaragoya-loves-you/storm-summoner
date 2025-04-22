idf_component_register (
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES
    analog_input
    cv
    display
    drv2605
    expression
    flicker
    i2c_common
    log
    lvgl
    stars
    touch
    uartmidi
    vcnl4040
)
