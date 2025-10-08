file(GLOB UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS ${UI_SRC_FILES}
  INCLUDE_DIRS "include"
  REQUIRES driver log cv app_settings event_bus tempo input_manager
  PRIV_REQUIRES main
)