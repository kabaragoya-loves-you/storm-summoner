file(GLOB UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")
# Ensure console source is included
list(APPEND UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/ui_console.c")
# Exclude compiled-in image data files (now loaded from LittleFS)
list(REMOVE_ITEM UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/earth.c")
list(REMOVE_ITEM UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/lizard.c")
# Exclude deprecated tempo modules (replaced by scene.c)
list(REMOVE_ITEM UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/tempo.c")
list(REMOVE_ITEM UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/tempo2.c")

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
)