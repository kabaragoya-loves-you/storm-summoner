file(GLOB_RECURSE LVGL_SRCS 
    "src/*.c"
    "src/*.cpp"
    "*.c"
)

idf_component_register(
    SRCS ${LVGL_SRCS} "polygon.c"
    INCLUDE_DIRS "." "src"
    REQUIRES driver esp_timer esp_driver_ppa esp_mm hal
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    "-DLV_CONF_INCLUDE_SIMPLE"
    "-DENABLE_PERFORMANCE_MONITORING=${ENABLE_PERFORMANCE_MONITORING}"
)

# Suppress warnings in third-party ThorVG code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-type-limits
)