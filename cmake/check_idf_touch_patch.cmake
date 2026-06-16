# Require the local ESP-IDF touch channel 14 patch before configuring the project.
# See: components/hal/esp32p4/include/hal/touch_sensor_ll.h
#      HAL_ASSERT(curr_chan <= 14)  (stock IDF has < 14)

if(NOT DEFINED ENV{IDF_PATH})
  message(FATAL_ERROR "IDF_PATH is not set. Use the ESP-IDF extension terminal or export.bat.")
endif()

set(_touch_ll_h "$ENV{IDF_PATH}/components/hal/esp32p4/include/hal/touch_sensor_ll.h")
if(NOT EXISTS "${_touch_ll_h}")
  return()
endif()

file(READ "${_touch_ll_h}" _touch_ll_src)
if(_touch_ll_src MATCHES "HAL_ASSERT\\(curr_chan < 14\\)")
  message(FATAL_ERROR
    "ESP-IDF touch channel 14 patch is missing.\n"
    "  IDF_PATH=$ENV{IDF_PATH}\n"
    "  file: ${_touch_ll_h}\n"
    "Change HAL_ASSERT(curr_chan < 14) to HAL_ASSERT(curr_chan <= 14).\n"
    "Also ensure idf.espIdfPathWin and idf.currentSetup point at the same IDF root.")
endif()

unset(_touch_ll_h)
unset(_touch_ll_src)
