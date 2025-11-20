file(GLOB UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")
# Ensure console source is included
list(APPEND UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/ui_console.c")

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
)