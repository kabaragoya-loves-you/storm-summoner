file(GLOB UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")
list(APPEND UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/ui_console.c")
# Explicit so CMake picks up new modules without a stale GLOB cache
list(APPEND UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/screw_calibrate.c")

idf_component_register(
    SRCS
        ${UI_SRC_FILES}
    INCLUDE_DIRS
        "include"
    REQUIRES
        "lvgl"
        "touch"
        "event_bus"
        "app_settings"
        "console"
        "menu"
        "display"
        "miniz"
        "tempo"
        "transport"
        "scene"
        "action"
        "cc_state"
        "assets_manager"
        "lfo"
        "version"
        "memory_utils"
        "task_monitor"
        "cv"
        "rtg"
        "usb_cdc_update"
)
