# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(storm-summoner)

# Add components directory to build
set(EXTRA_COMPONENT_DIRS
    components/display
    components/stars
    components/touch
    components/drv2605_manager
    components/flicker
    components/analog_input
    components/cv
    components/expression
    components/vcnl4040
    components/uartmidi
    components/midi_in
    components/midi_tempo
)

# Add main directory to build
set(COMPONENT_DIRS main)