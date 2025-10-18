idf_component_register(SRCS "scene_test.c" "scene.c"
  INCLUDE_DIRS "include"
  REQUIRES midi event_bus
  PRIV_REQUIRES main)


