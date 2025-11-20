idf_component_register(
  SRCS
    "menu.c"
    "pages/index.c"
    "pages/scenes.c"
    "pages/device_config.c"
    "pages/settings.c"
    "pages/about.c"
  INCLUDE_DIRS
    "include"
  REQUIRES
    lvgl
    ui
    scene
)

