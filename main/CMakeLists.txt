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
set(ASSETS_BUILD_DIR "${CMAKE_BINARY_DIR}/assets_combined")
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}")

# Always refresh the staging directory before packing the LittleFS image.
# Configure-time globs and stamp outputs miss newly added files; this runs
# every build (same pattern as increment_build_number in the root CMakeLists).
add_custom_target(prep_assets ALL
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DASSETS_BUILD_DIR=${ASSETS_BUILD_DIR}
    -P ${CMAKE_SOURCE_DIR}/cmake/prep_assets.cmake
  COMMENT "Preparing assets (manifest, devices, images, factory scenes)"
)

# Create LittleFS partition image from combined assets directory.
# Contains only the read-only shared content (devices, images, factory
# scene presets); the device formats `userdata` from empty on first boot
# and seeds `default.json` from baked-in code.
# To flash: idf.py assets-flash
littlefs_create_partition_image(assets ${ASSETS_BUILD_DIR}
  DEPENDS prep_assets
)
