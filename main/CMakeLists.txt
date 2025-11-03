# Register main component with all project components explicitly listed
idf_component_register(
    SRCS "main.c" "usb_descriptors.c"
    INCLUDE_DIRS "."
    REQUIRES 
      adc_manager app_settings assets_manager bump buttons clock_sync console_repl cv dac
      display elite event_bus expression firmware_update haptic i2c_common
      input_manager led lvgl midi monitor revision scene screensaver sensor
      stars switch task_monitor tempo tinyusb_init touch transport ui usb_mode
      esp_tinyusb_wrapper
)

# Collect all JSON files from midi-devices directory
file(GLOB_RECURSE MIDI_DEVICE_FILES "${CMAKE_SOURCE_DIR}/midi-devices/*.json")

# Create LittleFS partition image from midi-devices directory
# Only rebuilds if any JSON file changes
# To flash: idf.py assets-flash
littlefs_create_partition_image(assets ${CMAKE_SOURCE_DIR}/midi-devices
  DEPENDS ${MIDI_DEVICE_FILES}
)