idf_component_register(
  SRCS "version.c" "version_console.c"
  INCLUDE_DIRS "include" "${CMAKE_BINARY_DIR}"
  REQUIRES "esp_hw_support" "console" "app_settings"
)

# Build number auto-increment. Defined HERE, in the same CMake directory as
# version.c, on purpose: a custom-command OUTPUT only forces its consumer to
# recompile when producer and consumer share scope. version.c #includes the
# generated version_build.h and lists it via OBJECT_DEPENDS, and the command's
# phony second output (never actually created) keeps the command perpetually
# dirty. So on every build Ninja: (1) re-runs the command, incrementing
# build_number.txt and regenerating the header, then (2) recompiles version.c
# against the fresh number.
set(BUILD_NUMBER_FILE "${CMAKE_SOURCE_DIR}/build_number.txt")
set(BUILD_NUMBER_HEADER "${CMAKE_BINARY_DIR}/version_build.h")

add_custom_command(
  OUTPUT "${BUILD_NUMBER_HEADER}" "${CMAKE_BINARY_DIR}/_always_increment_build"
  COMMAND ${CMAKE_COMMAND}
    -DBUILD_NUMBER_FILE=${BUILD_NUMBER_FILE}
    -DBUILD_NUMBER_HEADER=${BUILD_NUMBER_HEADER}
    -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
  COMMENT "Incrementing build number"
  VERBATIM
)

set_property(SOURCE "version.c" APPEND PROPERTY
  OBJECT_DEPENDS "${BUILD_NUMBER_HEADER}")
