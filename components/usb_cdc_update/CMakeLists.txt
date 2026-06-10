idf_component_register(
  SRCS "usb_cdc_update.c"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "."
  REQUIRES
    espressif__tinyusb
    firmware_update
    console
    assets_manager
    joltwallet__littlefs
    miniz
    lvgl_stream
    display
    event_bus
    midi
    device_config
    app_settings
    mbedtls
    settings_registry
    scene
    action
    config
    json
    transport
    tempo
    task_monitor
    cv
    expression
    input_manager
    midi
  PRIV_REQUIRES
    main
    screensaver
)

