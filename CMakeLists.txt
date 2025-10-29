# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(ENABLE_PERFORMANCE_MONITORING 0)  # Enable performance monitoring across all components

project(storm-summoner)

set(COMPONENT_DIRS main)