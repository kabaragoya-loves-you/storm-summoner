file(GLOB DISPLAY_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS
    ${DISPLAY_SRC_FILES}
  INCLUDE_DIRS "include"
  REQUIRES driver lvgl esp_timer log console
  PRIV_REQUIRES main
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DENABLE_PERFORMANCE_MONITORING=${ENABLE_PERFORMANCE_MONITORING}
    -DENABLE_SPI_DMA=1
)
