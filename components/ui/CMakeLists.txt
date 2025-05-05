idf_component_register(
    SRCS 
        "ui.c" "draw_image.c"
    INCLUDE_DIRS 
        "include"
    REQUIRES
        "lvgl"
)