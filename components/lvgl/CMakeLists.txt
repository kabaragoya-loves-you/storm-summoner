file(GLOB_RECURSE LVGL_SRCS 
    "src/*.c"
    "*.c"
)

idf_component_register(
    SRCS ${LVGL_SRCS} "polygon.c"
    INCLUDE_DIRS "." "src"
    REQUIRES driver esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    "-DLV_CONF_INCLUDE_SIMPLE"
    "-DENABLE_PERFORMANCE_MONITORING=${CONFIG_ENABLE_PERFORMANCE_MONITORING}"
)