idf_component_register(
  SRCS
    "ssd1327_driver.c" "display.c"
  INCLUDE_DIRS "include"
  REQUIRES driver lvgl esp_timer log
)