idf_component_register(SRCS "scene_test.c" "scene.c" "scene_console.c" "continuous_mapping.c" "smart_filter.c" "scene_name_gen.c"
  INCLUDE_DIRS "include"
  REQUIRES midi event_bus device_config config app_settings action curve console memory_utils lfo rtg sample_hold lis3dhtr
  PRIV_REQUIRES main ui)


