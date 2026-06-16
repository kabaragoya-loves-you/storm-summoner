# check_device_manifest.cmake
# Called at build time to regenerate manifest.json when the device file count changes.
#
# Required -D args:
#   DEVICES_DIR        - path to midi-devices/devices
#   DEVICE_COUNT_FILE  - path to repo-root device_count.txt
#   MANIFEST_SCRIPT    - path to midi-devices/tools/build_manifest.rb
#   MANIFEST_ROOT      - path to midi-devices (build_manifest.rb --root)
#   MANIFEST_FILE      - path to midi-devices/manifest.json

file(GLOB_RECURSE DEVICE_FILES "${DEVICES_DIR}/*.json")

set(DEVICE_FILES_FILTERED "")
foreach(fp IN LISTS DEVICE_FILES)
  string(REPLACE "\\" "/" fp_norm "${fp}")
  if(NOT fp_norm MATCHES "/user/")
    list(APPEND DEVICE_FILES_FILTERED "${fp}")
  endif()
endforeach()

list(LENGTH DEVICE_FILES_FILTERED CURRENT_COUNT)

if(EXISTS "${DEVICE_COUNT_FILE}")
  file(READ "${DEVICE_COUNT_FILE}" STORED_COUNT)
  string(STRIP "${STORED_COUNT}" STORED_COUNT)
else()
  set(STORED_COUNT -1)
endif()

set(NEED_REGEN FALSE)
if(NOT EXISTS "${MANIFEST_FILE}")
  set(NEED_REGEN TRUE)
elseif(NOT CURRENT_COUNT EQUAL STORED_COUNT)
  set(NEED_REGEN TRUE)
endif()

if(NEED_REGEN)
  find_program(RUBY_EXECUTABLE ruby)
  if(NOT RUBY_EXECUTABLE)
    message(FATAL_ERROR "Ruby not found; required to regenerate device manifest")
  endif()

  message(STATUS "Device count changed (${STORED_COUNT} -> ${CURRENT_COUNT}); regenerating manifest")
  execute_process(
    COMMAND ${RUBY_EXECUTABLE} ${MANIFEST_SCRIPT} --root ${MANIFEST_ROOT}
    RESULT_VARIABLE MANIFEST_RC
  )
  if(NOT MANIFEST_RC EQUAL 0)
    message(FATAL_ERROR "build_manifest.rb failed (exit ${MANIFEST_RC})")
  endif()

  file(WRITE "${DEVICE_COUNT_FILE}" "${CURRENT_COUNT}\n")
else()
  message(STATUS "Device count unchanged (${CURRENT_COUNT}); skipping manifest regen")
endif()
