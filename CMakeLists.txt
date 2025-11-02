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
set(HW_CONFIG_PRODUCTION 0)
set(HW_CONFIG_DEV_BOARD 1)

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

# Add linker flags to include entire libraries for components with indirect calls
target_link_options(${elf} PRIVATE 
    "LINKER:--whole-archive"
    "$<TARGET_FILE:__idf_stars>"
    "$<TARGET_FILE:__idf_elite>"
    "$<TARGET_FILE:__idf_clock_sync>"
    "$<TARGET_FILE:__idf_scene>"
    "LINKER:--no-whole-archive"
)