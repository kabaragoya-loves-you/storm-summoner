idf_component_register(
  SRCS "console_repl.c" "console_completion.c"
  INCLUDE_DIRS "include"
  REQUIRES 
    console
    scene
    device_config
    app_settings
    revision
    i2c_common
    input_manager
    buttons
    touch
    switch
    bump
    sensor
    expression
    cv
    dac
    midi
    tempo
    transport
    clock_sync
    led
    haptic
    display
    ui
    screensaver
    assets_manager
    firmware_update
    usb_mode
    event_bus
    driver
)

