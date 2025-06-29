idf_component_register(
  SRCS
    "ssd1327_driver.c" "display.c" "coordinate_map.c" "circular_display.c" "performance.c" "sparse_buffer.c"
  INCLUDE_DIRS "include"
  REQUIRES driver lvgl esp_timer log
  PRIV_REQUIRES main
)

# Add compile definitions from ssd1327_driver.h configuration
# These need to be available globally for LVGL configuration
target_compile_definitions(${COMPONENT_LIB} PUBLIC 
    -DENABLE_PERFORMANCE_MONITORING=0
    -DDISPLAY_OPTIMIZATION_MODE=0
    -DENABLE_SPI_DMA=0
)