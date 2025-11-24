idf_component_register(
  SRCS "tinyusb_init.c"
  INCLUDE_DIRS "include"
  REQUIRES
    espressif__tinyusb
    esp_driver_gpio
    esp_driver_usb_serial_jtag
  PRIV_REQUIRES
    main
    usb
)

# Add tusb_config.h path
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/main)


