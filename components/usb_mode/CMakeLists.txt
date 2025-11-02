idf_component_register(
  SRCS "usb_mode_manager.c"
  INCLUDE_DIRS "include"
  REQUIRES driver log esp_tinyusb_wrapper firmware_update
  PRIV_REQUIRES midi main
)

# Add tusb_config.h path
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/main)


