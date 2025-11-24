# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

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
    "$<TARGET_FILE:__idf_scene>"
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
    "$<TARGET_FILE:__idf_dac>"
    "$<TARGET_FILE:__idf_midi>"
    "$<TARGET_FILE:__idf_tempo>"
    "$<TARGET_FILE:__idf_transport>"
    "$<TARGET_FILE:__idf_clock_sync>"
    "$<TARGET_FILE:__idf_led>"
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
    "LINKER:--no-whole-archive"
)