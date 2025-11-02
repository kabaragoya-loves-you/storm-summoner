idf_component_register(
  SRCS "tinyusb_init.c"
  INCLUDE_DIRS "include"
  REQUIRES esp_tinyusb_wrapper log
)

# Add tusb_config.h path
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/main)


