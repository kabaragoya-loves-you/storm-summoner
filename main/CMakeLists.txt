idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)

# Collect all JSON files from midi-devices directory
file(GLOB_RECURSE MIDI_DEVICE_FILES "${CMAKE_SOURCE_DIR}/midi-devices/*.json")

# Create LittleFS partition image from midi-devices directory
# Only rebuilds if any JSON file changes
# To flash: idf.py assets-flash
littlefs_create_partition_image(assets ${CMAKE_SOURCE_DIR}/midi-devices
  DEPENDS ${MIDI_DEVICE_FILES}
)