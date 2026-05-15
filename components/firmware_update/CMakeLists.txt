idf_component_register(
  SRCS "firmware_update.c" "firmware_update_console.c"
  INCLUDE_DIRS "include"
  REQUIRES app_update log console bootloader_support spi_flash esp_partition version
)

