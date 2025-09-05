file(GLOB UI_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS ${UI_SRC_FILES}
  INCLUDE_DIRS "include"
  REQUIRES driver log cv analog_input app_settings event_bus tempo
  PRIV_REQUIRES main
)