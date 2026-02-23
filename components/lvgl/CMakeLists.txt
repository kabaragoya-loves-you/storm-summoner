# LVGL CMakeLists.txt - Optimized for ESP32-P4 project
# Only includes directories/files actually needed to reduce build time.
# See lv_conf.h for which features are enabled.

# Custom project files at root level (fonts, polygon)
file(GLOB LVGL_ROOT_SRCS "*.c")

# Core LVGL sources (always needed)
file(GLOB_RECURSE LVGL_CORE_SRCS
  "src/core/*.c"
  "src/display/*.c"
  "src/font/*.c"
  "src/indev/*.c"
  "src/layouts/*.c"
  "src/misc/*.c"
  "src/osal/*.c"
  "src/themes/*.c"
  "src/tick/*.c"
  "src/widgets/*.c"
)

# src/lv_init.c
file(GLOB LVGL_INIT_SRCS "src/*.c")

# Drawing - root files + software renderer
file(GLOB LVGL_DRAW_ROOT_SRCS "src/draw/*.c")
file(GLOB_RECURSE LVGL_DRAW_SW_SRCS "src/draw/sw/*.c")
file(GLOB_RECURSE LVGL_DRAW_CONVERT_SRCS "src/draw/convert/*.c")

# Espressif PPA accelerator (LV_USE_PPA = 1)
file(GLOB_RECURSE LVGL_DRAW_ESP_SRCS "src/draw/espressif/*.c")

# Stdlib - root files + builtin and clib (not micropython, rtthread, uefi)
file(GLOB LVGL_STDLIB_ROOT_SRCS "src/stdlib/*.c")
file(GLOB_RECURSE LVGL_STDLIB_SRCS
  "src/stdlib/builtin/*.c"
  "src/stdlib/clib/*.c"
)

# Libraries - bin_decoder + fsdrv (file system drivers)
file(GLOB_RECURSE LVGL_LIBS_SRCS
  "src/libs/bin_decoder/*.c"
  "src/libs/fsdrv/*.c"
)

# Others - only observer (LV_USE_OBSERVER = 1) and sysmon (LV_USE_SYSMON)
file(GLOB_RECURSE LVGL_OTHERS_SRCS
  "src/others/observer/*.c"
  "src/others/sysmon/*.c"
)

# Combine all sources
set(LVGL_SRCS
  ${LVGL_ROOT_SRCS}
  ${LVGL_INIT_SRCS}
  ${LVGL_CORE_SRCS}
  ${LVGL_DRAW_ROOT_SRCS}
  ${LVGL_DRAW_SW_SRCS}
  ${LVGL_DRAW_CONVERT_SRCS}
  ${LVGL_DRAW_ESP_SRCS}
  ${LVGL_STDLIB_ROOT_SRCS}
  ${LVGL_STDLIB_SRCS}
  ${LVGL_LIBS_SRCS}
  ${LVGL_OTHERS_SRCS}
)

idf_component_register(
  SRCS ${LVGL_SRCS}
  INCLUDE_DIRS "." "src"
  REQUIRES driver esp_timer esp_driver_ppa esp_mm hal
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  "-DLV_CONF_INCLUDE_SIMPLE"
  "-DENABLE_PERFORMANCE_MONITORING=${ENABLE_PERFORMANCE_MONITORING}"
)

# Suppress warnings in third-party code
target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-type-limits
)
