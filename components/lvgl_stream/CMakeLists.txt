idf_component_register(
  SRCS "lvgl_stream.c"
  INCLUDE_DIRS "include"
  REQUIRES lvgl
  PRIV_REQUIRES espressif__tinyusb main
)
