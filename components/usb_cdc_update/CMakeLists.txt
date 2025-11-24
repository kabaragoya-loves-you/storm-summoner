idf_component_register(
  SRCS "usb_cdc_update.c"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "."
  REQUIRES
    espressif__tinyusb
    firmware_update
  PRIV_REQUIRES
    main
)

