# Register main component with all project components explicitly listed
idf_component_register(
    SRCS "main.c" "ldo.c" "usb_descriptors.c"
    INCLUDE_DIRS "."
    REQUIRES 
      adc_manager action app_settings assets_manager lis3dhtr buttons clock_sync config console_repl curve cv dac
      display elite event_bus expression firmware_update haptic i2c_common
      input_manager lvgl midi monitor note_track revision rtg scene screensaver sensor
      stars switch task_monitor tempo tinyusb_init touch transport ui usb_cdc_update version
      espressif__tinyusb
)

# The `assets` partition is now strictly read-only. It holds the shared
# MIDI device DB, UI images, and the factory-preset scene library.
# User-authored scenes and cloned/edited pedals live in the separate
# `userdata` partition and are NEVER baked into the assets image.
file(GLOB_RECURSE MIDI_DEVICE_FILES "${CMAKE_SOURCE_DIR}/midi-devices/*.json")
file(GLOB_RECURSE IMAGE_FILES "${CMAKE_SOURCE_DIR}/images/*.bin" "${CMAKE_SOURCE_DIR}/images/*.bin.z" "${CMAKE_SOURCE_DIR}/images/*.svg")
file(GLOB FACTORY_SCENE_FILES "${CMAKE_SOURCE_DIR}/scenes/factory/*.json")

set(ASSETS_BUILD_DIR "${CMAKE_BINARY_DIR}/assets_combined")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/devices")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/images")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/scenes")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/scenes/factory")

# Copy midi-devices to assets/devices, but explicitly drop the `user/` subtree.
# User-defined pedals (the seed `default.json` and any clones) live on the RW
# `userdata` partition; baking them into the RO image would defeat the split.
add_custom_command(
  OUTPUT "${ASSETS_BUILD_DIR}/devices/.copied"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/midi-devices"
    "${ASSETS_BUILD_DIR}/devices"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_BUILD_DIR}/devices/tools"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${ASSETS_BUILD_DIR}/devices/user"
  COMMAND ${CMAKE_COMMAND} -E remove -f "${ASSETS_BUILD_DIR}/devices/README.md"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_BUILD_DIR}/devices/.copied"
  DEPENDS ${MIDI_DEVICE_FILES}
  COMMENT "Copying shared MIDI device files to build assets (excluding user/)"
)

# Copy images to assets/images
add_custom_command(
  OUTPUT "${ASSETS_BUILD_DIR}/images/.copied"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/images"
    "${ASSETS_BUILD_DIR}/images"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_BUILD_DIR}/images/.copied"
  DEPENDS ${IMAGE_FILES}
  COMMENT "Copying image files to build assets"
)

# Copy factory-preset scenes to assets/scenes/factory. These are seeded into
# /userdata/scenes/ as inactive entries on first boot (see scene.c). Empty
# folder is fine; the firmware no-ops if there's nothing to seed.
add_custom_command(
  OUTPUT "${ASSETS_BUILD_DIR}/scenes/factory/.copied"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/scenes/factory"
    "${ASSETS_BUILD_DIR}/scenes/factory"
  COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_BUILD_DIR}/scenes/factory/.copied"
  DEPENDS ${FACTORY_SCENE_FILES}
  COMMENT "Copying factory preset scenes to build assets"
)

# Create LittleFS partition image from combined assets directory.
# Contains only the read-only shared content (devices, images, factory
# scene presets); the device formats `userdata` from empty on first boot
# and seeds `default.json` from baked-in code.
# To flash: idf.py assets-flash
littlefs_create_partition_image(assets ${ASSETS_BUILD_DIR}
  DEPENDS
    "${ASSETS_BUILD_DIR}/devices/.copied"
    "${ASSETS_BUILD_DIR}/images/.copied"
    "${ASSETS_BUILD_DIR}/scenes/factory/.copied"
)