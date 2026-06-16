# prep_assets.cmake
# Called on every build to refresh the assets staging directory.
#
# Required -D args:
#   SOURCE_DIR       - project root
#   ASSETS_BUILD_DIR - ${CMAKE_BINARY_DIR}/assets_combined

set(DEVICES_DIR "${SOURCE_DIR}/midi-devices/devices")
set(DEVICE_COUNT_FILE "${SOURCE_DIR}/device_count.txt")
set(MANIFEST_SCRIPT "${SOURCE_DIR}/midi-devices/tools/build_manifest.rb")
set(MANIFEST_ROOT "${SOURCE_DIR}/midi-devices")
set(MANIFEST_FILE "${SOURCE_DIR}/midi-devices/manifest.json")

set(DEVICES_STAGING "${ASSETS_BUILD_DIR}/devices")
set(IMAGES_STAGING "${ASSETS_BUILD_DIR}/images")
set(FACTORY_SCENES_STAGING "${ASSETS_BUILD_DIR}/scenes/factory")

include("${CMAKE_CURRENT_LIST_DIR}/check_device_manifest.cmake")

function(prep_assets_run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "prep_assets failed: ${ARGN}")
  endif()
endfunction()

# Copy midi-devices to assets/devices, but explicitly drop the `user/` subtree.
# User-defined pedals live on the RW `userdata` partition.
file(MAKE_DIRECTORY "${ASSETS_BUILD_DIR}/scenes")
prep_assets_run(${CMAKE_COMMAND} -E remove_directory "${DEVICES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E make_directory "${DEVICES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E copy_directory "${SOURCE_DIR}/midi-devices" "${DEVICES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E remove_directory "${DEVICES_STAGING}/tools")
prep_assets_run(${CMAKE_COMMAND} -E remove_directory "${DEVICES_STAGING}/user")
prep_assets_run(${CMAKE_COMMAND} -E remove -f "${DEVICES_STAGING}/README.md")

prep_assets_run(${CMAKE_COMMAND} -E remove_directory "${IMAGES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E make_directory "${IMAGES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E copy_directory "${SOURCE_DIR}/images" "${IMAGES_STAGING}")

prep_assets_run(${CMAKE_COMMAND} -E remove_directory "${FACTORY_SCENES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E make_directory "${FACTORY_SCENES_STAGING}")
prep_assets_run(${CMAKE_COMMAND} -E copy_directory "${SOURCE_DIR}/scenes/factory" "${FACTORY_SCENES_STAGING}")
