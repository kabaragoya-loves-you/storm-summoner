file(GLOB MIDI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS ${MIDI_SRC_FILES}
  INCLUDE_DIRS "include"
  REQUIRES driver log cv app_settings event_bus tempo input_manager scene action firmware_update esp_tinyusb_wrapper revision console assets_manager
  PRIV_REQUIRES main device_config
)

# Add tusb_config.h path to MIDI component for USB files
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_SOURCE_DIR}/main)