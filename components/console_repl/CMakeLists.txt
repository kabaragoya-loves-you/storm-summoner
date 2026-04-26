idf_component_register(
  SRCS "console_repl.c" "console_completion.c"
  INCLUDE_DIRS "include"
  REQUIRES 
    console
    config
    scene
    device_config
    app_settings
    revision
    i2c_common
    input_manager
    buttons
    touch
    switch
    lis3dhtr
    sensor
    expression
    cv
    dac
    adc_manager
    midi
    tempo
    transport
    clock_sync
    haptic
    display
    ui
    screensaver
    assets_manager
    firmware_update
    event_bus
    driver
    version
    lfo
    sample_hold
)

