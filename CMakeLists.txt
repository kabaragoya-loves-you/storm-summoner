# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# =============================================================================
# Firmware Version Configuration
# =============================================================================
set(FW_VERSION_MAJOR 0)
set(FW_VERSION_MINOR 1)

# Get short git hash (captured at configure time - rebuild after commits to update)
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE FW_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  # Check for dirty working tree
  execute_process(
    COMMAND ${GIT_EXECUTABLE} diff --quiet HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE GIT_DIRTY
  )
  if(GIT_DIRTY)
    set(FW_GIT_HASH "${FW_GIT_HASH}-dirty")
  endif()
else()
  set(FW_GIT_HASH "unknown")
endif()

message(STATUS "Firmware Version: ${FW_VERSION_MAJOR}.${FW_VERSION_MINOR} (${FW_GIT_HASH})")
# =============================================================================

set(ENABLE_PERFORMANCE_MONITORING 0)  # Enable performance monitoring across all components

# Hardware configuration: set ONE of these to 1
# HW_CONFIG_PRODUCTION - Production hardware configuration  
# HW_CONFIG_DEV_BOARD - Development board configuration
set(HW_CONFIG_PRODUCTION 1)
set(HW_CONFIG_DEV_BOARD 0)

project(storm-summoner)

set(COMPONENT_DIRS main)

# Force-include specific component libraries that are called indirectly to prevent garbage collection
idf_build_get_property(build_components BUILD_COMPONENTS)
idf_build_get_property(elf EXECUTABLE)

# Apply hardware configuration as global compile definitions
if(HW_CONFIG_PRODUCTION)
    idf_build_set_property(COMPILE_DEFINITIONS "-DHW_CONFIG_PRODUCTION=1" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "-DHW_CONFIG_DEV_BOARD=0" APPEND)
else()
    idf_build_set_property(COMPILE_DEFINITIONS "-DHW_CONFIG_PRODUCTION=0" APPEND)
    idf_build_set_property(COMPILE_DEFINITIONS "-DHW_CONFIG_DEV_BOARD=1" APPEND)
endif()

# Apply version information as global compile definitions
idf_build_set_property(COMPILE_DEFINITIONS "-DFW_VERSION_MAJOR=${FW_VERSION_MAJOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DFW_VERSION_MINOR=${FW_VERSION_MINOR}" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DFW_GIT_HASH=\"${FW_GIT_HASH}\"" APPEND)

# Build number auto-increment: runs on every build via custom target
set(BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build_number.txt")
set(BUILD_NUMBER_HEADER "${CMAKE_BINARY_DIR}/version_build.h")

add_custom_target(increment_build_number ALL
  COMMAND ${CMAKE_COMMAND} 
    -DBUILD_NUMBER_FILE=${BUILD_NUMBER_FILE}
    -DBUILD_NUMBER_HEADER=${BUILD_NUMBER_HEADER}
    -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
  COMMENT "Incrementing build number"
)

# Make the ELF depend on the build number target
add_dependencies(${elf} increment_build_number)

# Force enable TinyUSB classes globally to ensure the library compiles with them
# even if disabled in Kconfig to avoid wrapper interference
idf_build_set_property(COMPILE_DEFINITIONS "-DCFG_TUD_MIDI=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DCFG_TUD_CDC=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "-DCFG_TUD_MSC=0" APPEND)

# Add linker flags to include entire libraries for components with indirect calls
target_link_options(${elf} PRIVATE 
    "LINKER:--whole-archive"
    "$<TARGET_FILE:__idf_stars>"
    "$<TARGET_FILE:__idf_elite>"
    "$<TARGET_FILE:__idf_plasma>"
    "$<TARGET_FILE:__idf_scene>"
    "$<TARGET_FILE:__idf_lfo>"
    "$<TARGET_FILE:__idf_device_config>"
    "$<TARGET_FILE:__idf_app_settings>"
    "$<TARGET_FILE:__idf_revision>"
    "$<TARGET_FILE:__idf_i2c_common>"
    "$<TARGET_FILE:__idf_input_manager>"
    "$<TARGET_FILE:__idf_adc_manager>"
    "$<TARGET_FILE:__idf_buttons>"
    "$<TARGET_FILE:__idf_touch>"
    "$<TARGET_FILE:__idf_switch>"
    "$<TARGET_FILE:__idf_bump>"
    "$<TARGET_FILE:__idf_sensor>"
    "$<TARGET_FILE:__idf_expression>"
    "$<TARGET_FILE:__idf_cv>"
    "$<TARGET_FILE:__idf_sample_hold>"
    "$<TARGET_FILE:__idf_rtg>"
    "$<TARGET_FILE:__idf_dac>"
    "$<TARGET_FILE:__idf_midi>"
    "$<TARGET_FILE:__idf_tempo>"
    "$<TARGET_FILE:__idf_transport>"
    "$<TARGET_FILE:__idf_clock_sync>"
    "$<TARGET_FILE:__idf_haptic>"
    "$<TARGET_FILE:__idf_display>"
    "$<TARGET_FILE:__idf_ui>"
    "$<TARGET_FILE:__idf_screensaver>"
    "$<TARGET_FILE:__idf_assets_manager>"
    "$<TARGET_FILE:__idf_firmware_update>"
    "$<TARGET_FILE:__idf_usb_cdc_update>"
    "$<TARGET_FILE:__idf_event_bus>"
    "$<TARGET_FILE:__idf_action>"
    "$<TARGET_FILE:__idf_config>"
    "$<TARGET_FILE:__idf_version>"
    "$<TARGET_FILE:__idf_miniz>"
    "$<TARGET_FILE:__idf_settings_registry>"
    "LINKER:--no-whole-archive"
)