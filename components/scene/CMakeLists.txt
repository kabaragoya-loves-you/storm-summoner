idf_component_register(SRCS "scene_test.c" "scene.c" "scene_console.c" "scene_assign_console.c" "continuous_mapping.c"
  INCLUDE_DIRS "include"
  REQUIRES midi event_bus device_config app_settings action curve console
  PRIV_REQUIRES main)


