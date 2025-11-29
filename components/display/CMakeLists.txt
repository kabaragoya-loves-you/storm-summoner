file(GLOB DISPLAY_SRC_FILES "${CMAKE_CURRENT_LIST_DIR}/*.c")

idf_component_register(
  SRCS
    ${DISPLAY_SRC_FILES}
  INCLUDE_DIRS "include"
  REQUIRES driver lvgl esp_timer log console revision
  PRIV_REQUIRES main
)

# Add compile definitions from ssd1327_driver.h configuration
# These need to be available globally for LVGL configuration
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DENABLE_PERFORMANCE_MONITORING=${ENABLE_PERFORMANCE_MONITORING}
    -DDISPLAY_OPTIMIZATION_MODE=0
    -DENABLE_SPI_DMA=1
)