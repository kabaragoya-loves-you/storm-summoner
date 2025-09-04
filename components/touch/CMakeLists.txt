idf_component_register(
  SRCS
    # "touch.c"  # Temporarily disabled while testing simplified version
    "touch_simple.c"
  INCLUDE_DIRS 
    "include"
  REQUIRES 
    log 
    driver 
    esp_timer 
    freertos
    app_settings
    touch_spi_master
    event_bus
  PRIV_REQUIRES 
    main
)