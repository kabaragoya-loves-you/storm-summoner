# Register main component with all project components explicitly listed
idf_component_register(
    SRCS "main.c" "usb_descriptors.c"
    INCLUDE_DIRS "."
    REQUIRES 
      adc_manager action app_settings assets_manager bump buttons clock_sync console_repl curve cv dac
      display elite event_bus expression firmware_update haptic i2c_common
      input_manager led lvgl midi monitor revision scene screensaver sensor
      stars switch task_monitor tempo tinyusb_init touch transport ui usb_cdc_update
      espressif__tinyusb
)

# Collect all JSON files from midi-devices and scenes directories
file(GLOB_RECURSE MIDI_DEVICE_FILES "${CMAKE_SOURCE_DIR}/midi-devices/*.json")
file(GLOB_RECURSE SCENE_FILES "${CMAKE_SOURCE_DIR}/scenes/*.json")

# Create a temporary combined assets directory at build time
set(ASSETS_BUILD_DIR "${CMAKE_BINARY_DIR}/assets_combined")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/devices")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/scenes")

# Copy midi-devices to assets/devices
add_custom_command(
  OUTPUT "${ASSETS_BUILD_DIR}/devices/.copied"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/midi-devices"
    "${ASSETS_BUILD_DIR}/devices"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_BUILD_DIR}/devices/tools"
  COMMAND ${CMAKE_COMMAND} -E remove -f "${ASSETS_BUILD_DIR}/devices/README.md"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_BUILD_DIR}/devices/.copied"
  DEPENDS ${MIDI_DEVICE_FILES}
  COMMENT "Copying MIDI device files to build assets"
)

# Copy scenes to assets/scenes
add_custom_command(
  OUTPUT "${ASSETS_BUILD_DIR}/scenes/.copied"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/scenes"
    "${ASSETS_BUILD_DIR}/scenes"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_BUILD_DIR}/scenes/tools"
  COMMAND ${CMAKE_COMMAND} -E remove -f "${ASSETS_BUILD_DIR}/scenes/README.md"
  COMMAND ${CMAKE_COMMAND} -E remove -f "${ASSETS_BUILD_DIR}/scenes/.gitignore"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_BUILD_DIR}/scenes/.copied"
  DEPENDS ${SCENE_FILES}
  COMMENT "Copying scene files to build assets"
)

# Create LittleFS partition image from combined assets directory
# To flash: idf.py assets-flash
littlefs_create_partition_image(assets ${ASSETS_BUILD_DIR}
  DEPENDS 
    "${ASSETS_BUILD_DIR}/devices/.copied"
    "${ASSETS_BUILD_DIR}/scenes/.copied"
)