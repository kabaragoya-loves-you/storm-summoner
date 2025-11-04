idf_component_register(SRCS "scene_test.c" "scene.c" "scene_console.c"
  INCLUDE_DIRS "include"
  REQUIRES midi event_bus device_config app_settings action console
  PRIV_REQUIRES main)


