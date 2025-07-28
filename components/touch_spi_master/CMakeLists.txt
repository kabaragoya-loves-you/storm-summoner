idf_component_register(
  SRCS "touch_spi_master.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES "driver" "freertos" "esp_rom" "main"
) 